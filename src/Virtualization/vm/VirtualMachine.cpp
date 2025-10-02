#include "/home/hussin/Desktop/PenHive/include/Virtualization/vm/VirtualMachine.hpp"
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

VirtualMachine::VirtualMachine(std::shared_ptr<HypervisorConnector> conn, std::string_view vmName)
    : connector(std::move(conn)), name(vmName), domain(nullptr), state(VmState::Unknown) {
    refreshHandle();
    if (!domain) throw VmException("VM not found: " + name);
}

VirtualMachine::~VirtualMachine() {
    if (domain) virDomainFree(domain);
}

int VirtualMachine::setDomain(virDomainPtr Dom) {
    if (Dom == nullptr) return -1;
    if (domain) virDomainFree(domain);
    domain = Dom;
    return 0;
}

virDomainPtr VirtualMachine::getDomain() { return domain; }

void VirtualMachine::start() { checkLibvirtError(virDomainCreate(domain), "start"); }
void VirtualMachine::shutdown() { checkLibvirtError(virDomainShutdown(domain), "shutdown"); }
void VirtualMachine::reboot() { checkLibvirtError(virDomainReboot(domain, 0), "reboot"); }
void VirtualMachine::destroy() { checkLibvirtError(virDomainDestroy(domain), "destroy"); }

const std::string& VirtualMachine::getName() const noexcept { return name; }

VirtualMachine::VmState VirtualMachine::getState() const {
    int s;
    if (virDomainGetState(domain, &s, nullptr, 0) < 0) return VmState::Unknown;
    return mapLibvirtState(s);
}

bool VirtualMachine::isActive() const { return virDomainIsActive(domain) == 1; }
virDomainPtr VirtualMachine::getRawHandle() const noexcept { return domain; }

void VirtualMachine::refreshHandle() {
    if (domain) { virDomainFree(domain); domain = nullptr; }
    if (connector) {
        try {
            connector->ensureConnected();
            domain = virDomainLookupByName(connector->getRawHandle(), name.c_str());
        } catch (...) {
            domain = nullptr;
        }
    }
}

void VirtualMachine::checkLibvirtError(int result, const std::string& action) {
    if (result < 0) {
        virErrorPtr err = virGetLastError();
        throw LibvirtException(action + ": " + (err && err->message ? err->message : "unknown"));
    }
}

VirtualMachine::VmState VirtualMachine::mapLibvirtState(int state) {
    switch (state) {
        case VIR_DOMAIN_RUNNING: return VmState::Running;
        case VIR_DOMAIN_PAUSED: return VmState::Paused;
        case VIR_DOMAIN_SHUTOFF: return VmState::Shutdown;
        case VIR_DOMAIN_CRASHED: return VmState::Crashed;
        case VIR_DOMAIN_PMSUSPENDED: return VmState::Suspended;
        default: return VmState::Unknown;
    }
}

int VirtualMachine::bootstrap() { return 0; }
