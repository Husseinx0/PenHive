// src/model/VirtualMachine.hpp
#pragma once
#include <string>
#include <memory>
#include <libvirt/libvirt.h>
#include <vector>
#include "Virtualization/Utils/VmException.hpp"


using namespace std;
class VirtualMachine {
  
public:
enum class VmState {
    Running, Paused, Shutdown, Crashed, Suspended, Unknown
};
    VirtualMachine();
    ~VirtualMachine();

    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;
    [[nodiscard]] int setDomain(virDomainPtr Dom) {
        if (Dom == nullptr)
            return -1;
        domain = Dom;  // نقل الملكية
        return 0;
    }
    [[nodiscard]] virDomainPtr getDomain(){
        return domain;
    }

    
    int bootstrap();
    [[nodiscard]] const std::string& getName() const noexcept { return name; }
    [[nodiscard]] VmState getState() const;
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] virDomainPtr getRawHandle() const noexcept { return domain; }
private:
    struct VirDomainDeleter {
        void operator()(virDomainPtr dom) const {
            if (dom) virDomainFree(dom);
        }
    };
    std::shared_ptr<class HypervisorConnector> connector;
    virDomainPtr domain {nullptr};
    std::string name;

    void refreshHandle();
    void checkLibvirtError(int result, const std::string& action);
    static VmState mapLibvirtState(int state);


    /**
     *  The state of the virtual machine.
     */
    VmState state;
    // *************************************************************************
    // DataBase implementation (Private)
    // *************************************************************************

    /**
     * Bootstraps the database table(s) associated to the VirtualMachine
     *  @return 0 on success
     */
    int bootstrap();

};
/*
// src/model/VirtualMachine.cpp
#include "virtualization/VirtualMachine.hpp"
#include "System/Logger.hpp"

VirtualMachine::VirtualMachine(std::shared_ptr<HypervisorConnector> conn, std::string_view vmName)
    : connector(std::move(conn)), name(vmName) {
    refreshHandle();
    if (!domain) {
        throw VmException("VM not found: " + name);
    }
   
}

VirtualMachine::~VirtualMachine() {
    if (domain) virDomainFree(domain);
}

void VirtualMachine::refreshHandle() {
    if (domain) virDomainFree(domain);
    domain = virDomainLookupByName(connector->getRawHandle(), name.c_str());
}

void VirtualMachine::checkLibvirtError(int result, const std::string& action) {
    if (result < 0) {
        throw LibvirtException("Failed to " + action + " VM: " + name);
    }
}
#include <string>
void VirtualMachine::start() {
    
    VLOG_INFO("▶️ Starting VM: {}", name);
    int result = virDomainCreate(domain);
    checkLibvirtError(result, "start");
    VLOG_INFO("✅ VM started: {}", name);
}

void VirtualMachine::shutdown() {
    VLOG_INFO("⏹️ Shutting down VM: {}", name);
    int result = virDomainShutdown(domain);
    checkLibvirtError(result, "shutdown");
}

void VirtualMachine::reboot() {
    VLOG_INFO("🔄 Rebooting VM: {}", name);
    int result = virDomainReboot(domain, 0);
    checkLibvirtError(result, "reboot");
}

void VirtualMachine::destroy() {
    VLOG_INFO("💀 Force-stopping VM: {}", name);
    int result = virDomainDestroy(domain);
    checkLibvirtError(result, "destroy");
}

void VirtualMachine::suspend() {
    VLOG_INFO("⏸️ Suspending VM: {}", name);
    int result = virDomainSuspend(domain);
    checkLibvirtError(result, "suspend");
}

void VirtualMachine::resume() {
    VLOG_INFO("▶️ Resuming VM: {}", name);
    int result = virDomainResume(domain);
    checkLibvirtError(result, "resume");
}

bool VirtualMachine::isActive() const {
    return virDomainIsActive(domain) == 1;
}

VmState VirtualMachine::mapLibvirtState(int state) {
    switch (state) {
        case VIR_DOMAIN_RUNNING: return VmState::Running;
        case VIR_DOMAIN_PAUSED: return VmState::Paused;
        case VIR_DOMAIN_SHUTOFF: return VmState::Shutdown;
        case VIR_DOMAIN_CRASHED: return VmState::Crashed;
        case VIR_DOMAIN_PMSUSPENDED: return VmState::Suspended;
        default: return VmState::Unknown;
    }
}

VmState VirtualMachine::getState() const {
    int state;
    if (virDomainGetState(domain, &state, nullptr, 0) < 0) {
        return VmState::Unknown;
    }
    return mapLibvirtState(state);
}
*/