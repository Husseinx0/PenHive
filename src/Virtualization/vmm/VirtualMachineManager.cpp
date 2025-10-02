#include "/home/hussin/Desktop/PenHive/include/Virtualization/vmm/VirtualMachineManager.hpp"
#include <libvirt/libvirt.h>
#include <utility>

using namespace std::chrono_literals;

VirtualMachineManager::VirtualMachineManager(std::shared_ptr<HypervisorConnector> conn,
                                             std::shared_ptr<CONCURRENCY::EventDispatcher> dispatcher)
    : connector(std::move(conn)),
      vmpool(std::make_unique<VirtualMachinePool>(connector)),
      factory(std::make_unique<VirtualMachineFactory>(connector)),
      driver(std::make_unique<VirtualMachineDriver>(connector))
{
    if (dispatcher) {
        dispatcher_ = std::move(dispatcher);
        own_dispatcher_ = false;
    } else {
        dispatcher_ = std::make_shared<CONCURRENCY::EventDispatcher>(2); // default 2 threads
        own_dispatcher_ = true;
    }
    BoostLogger::Info("VirtualMachineManager initialized");
}

VirtualMachineManager::~VirtualMachineManager() {
    // Stop any owned dispatcher (EventDispatcher::stop is safe to call)
    try {
        if (own_dispatcher_ && dispatcher_) {
            dispatcher_->stop();
            // allow graceful shutdown
        }
    } catch (...) {
        // swallow
    }
    BoostLogger::Info("VirtualMachineManager destroyed");
}

Result<int> VirtualMachineManager::dispatch_deploy(const VmConfig& cfg) {
    std::scoped_lock lk(managerMutex);
    // ensure libvirt connection
    try {
        connector->connectOrThrow();
    } catch (const std::exception& e) {
        return Result<int>{std::string("Connector error: ") + e.what()};
    }

    // build XML
    auto xmlRes = factory->buildDomainXML(cfg);
    if (xmlRes.isErr()) return Result<int>{xmlRes.unwrapErr()};

    // define domain
    auto defRes = factory->defineDomain(xmlRes.unwrap());
    if (defRes.isErr()) return Result<int>{defRes.unwrapErr()};

    // allocate metadata record
    auto alloc = vmpool->allocate();
    if (alloc.isErr()) {
        // cleanup defined domain to avoid leak if needed
        virDomainPtr d = defRes.unwrap();
        if (d) {
            // try undefine (best-effort)
            virDomainUndefine(d);
            virDomainFree(d);
        }
        return Result<int>{alloc.unwrapErr()};
    }

    virDomainPtr domain = defRes.unwrap();
    // start domain
    if (!driver->startDomain(domain)) {
        // attempt cleanup
        virDomainUndefine(domain);
        virDomainFree(domain);
        return Result<int>{std::string("Failed to start domain")};
    }

    // free domain handle (management via name)
    virDomainFree(domain);

    BoostLogger::Info("Domain deployed: {}", cfg.name);
    return Result<int>{alloc.unwrap()};
}

void VirtualMachineManager::dispatch_deploy_async(const VmConfig& cfg, std::function<void(Result<int>)> callback) {
    // make a copy of cfg for background task
    VmConfig cfg_copy = cfg;
    auto dispatcher = dispatcher_;
    auto self = shared_from_this(); // NOTE: if class doesn't inherit enable_shared_from_this, this needs change
    // To avoid requiring enable_shared_from_this, capture weak_ptr-less: we assume manager lifetime > tasks or dispatcher owned.
    dispatcher->dispatch([this, cfg_copy = std::move(cfg_copy), callback = std::move(callback)]() mutable {
        auto res = this->dispatch_deploy(cfg_copy);
        if (callback) {
            try {
                callback(res);
            } catch (...) {
                // swallow callback exceptions
            }
        }
    });
}

std::shared_ptr<std::atomic<bool>> VirtualMachineManager::schedule_health_check(std::string vmName, std::chrono::seconds interval) {
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto dispatcher = dispatcher_;
    // recursive shared task
    auto task = std::make_shared<std::function<void()>>();
    *task = [this, dispatcher, vmName, interval, cancelFlag, task]() {
        if (cancelFlag->load()) return;
        // check VM state (synchronous call inside dispatcher thread)
        auto res = this->findDomainByName(vmName);
        if (res.isOk()) {
            auto vm = res.unwrap();
            auto state = vm->getState();
            BoostLogger::Info("HealthCheck: VM '{}' state: {}", vmName, static_cast<int>(state));
        } else {
            BoostLogger::Warn("HealthCheck: VM '{}' not found or error: {}", vmName, res.unwrapErr());
        }
        // schedule next tick
        if (!cancelFlag->load()) {
            dispatcher->dispatch_delayed(interval, [task]() {
                // call the stored function
                try {
                    (*task)();
                } catch (...) { /* swallow */ }
            });
        }
    };

    // start first check asynchronously
    dispatcher->dispatch([task]() {
        try { (*task)(); } catch (...) {}
    });

    return cancelFlag;
}

Result<std::unique_ptr<VirtualMachine>> VirtualMachineManager::findDomainByName(std::string_view name) {
    std::lock_guard<std::mutex> lk(managerMutex);
    if (!connector->isConnected()) {
        if (!connector->connect()) {
            return Result<std::unique_ptr<VirtualMachine>>{std::string("Failed to connect to hypervisor")};
        }
    }

    virDomainPtr domain = virDomainLookupByName(connector->getRawHandle(), name.data());
    if (!domain) {
        return Result<std::unique_ptr<VirtualMachine>>{std::string("Domain not found: " + std::string(name))};
    }
    virDomainFree(domain);
    return Result<std::unique_ptr<VirtualMachine>>{std::make_unique<VirtualMachine>(connector, std::string(name))};
}

Result<std::vector<std::unique_ptr<VirtualMachine>>> VirtualMachineManager::listAllDomains() {
    std::lock_guard<std::mutex> lk(managerMutex);
    if (!connector->isConnected()) {
        if (!connector->connect()) {
            return Result<std::vector<std::unique_ptr<VirtualMachine>>>{std::string("Failed to connect to hypervisor")};
        }
    }

    int count = virConnectNumOfDomains(connector->getRawHandle());
    if (count < 0) {
        return Result<std::vector<std::unique_ptr<VirtualMachine>>>{std::string("Failed to get domain count")};
    }
    if (count == 0) return Result<std::vector<std::unique_ptr<VirtualMachine>>>{std::vector<std::unique_ptr<VirtualMachine>>{}};

    std::vector<int> ids(static_cast<size_t>(count));
    int actual = virConnectListDomains(connector->getRawHandle(), ids.data(), count);
    if (actual < 0) {
        return Result<std::vector<std::unique_ptr<VirtualMachine>>>{std::string("Failed to list domains")};
    }

    std::vector<std::unique_ptr<VirtualMachine>> vms;
    vms.reserve(actual);

    for (int i = 0; i < actual; ++i) {
        virDomainPtr domain = virDomainLookupByID(connector->getRawHandle(), ids[i]);
        if (!domain) continue;
        const char* name = virDomainGetName(domain);
        if (name) {
            vms.push_back(std::make_unique<VirtualMachine>(connector, std::string(name)));
        }
        virDomainFree(domain);
    }

    return Result<std::vector<std::unique_ptr<VirtualMachine>>>{std::move(vms)};
}

Result<void> VirtualMachineManager::deleteDomain(std::string_view name, bool /*deleteStorage*/) {
    std::lock_guard<std::mutex> lk(managerMutex);
    auto vmRes = findDomainByName(name);
    if (vmRes.isErr()) {
        return Result<void>{vmRes.unwrapErr()};
    }
    auto vm = vmRes.unwrap();
    if (vm->isActive()) {
        if (virDomainDestroy(vm->getRawHandle()) < 0) {
            return Result<void>{std::string("Failed to destroy running domain: " + std::string(name))};
        }
    }
    if (virDomainUndefine(vm->getRawHandle()) < 0) {
        return Result<void>{std::string("Failed to undefine domain: " + std::string(name))};
    }
    return Result<void>{};
}