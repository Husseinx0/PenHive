#pragma once
#include <memory>
#include <libvirt/libvirt.h>
#include "Virtualization/vmm/HypervisorConnector.hpp"

class VirtualMachineDriver {
public:
    explicit VirtualMachineDriver(std::shared_ptr<HypervisorConnector> conn);
    ~VirtualMachineDriver();

    bool startDomain(virDomainPtr domain);
    bool shutdownDomain(virDomainPtr domain);
    bool destroyDomain(virDomainPtr domain);

private:
    std::shared_ptr<HypervisorConnector> connector;
};