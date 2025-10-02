#include "/home/hussin/Desktop/PenHive/include/Virtualization/vmm/VirtualMachineDriver.hpp"
#include <libvirt/libvirt.h>

VirtualMachineDriver::VirtualMachineDriver(std::shared_ptr<HypervisorConnector> conn)
    : connector(std::move(conn)) {}
VirtualMachineDriver::~VirtualMachineDriver() = default;

bool VirtualMachineDriver::startDomain(virDomainPtr domain) {
    if (!domain) return false;
    return virDomainCreate(domain) == 0;
}

bool VirtualMachineDriver::shutdownDomain(virDomainPtr domain) {
    if (!domain) return false;
    return virDomainShutdown(domain) == 0;
}

bool VirtualMachineDriver::destroyDomain(virDomainPtr domain) {
    if (!domain) return false;
    return virDomainDestroy(domain) == 0;
}
