#pragma once
#include <libvirt/libvirt.h>
#include <memory>
#include <string>
#include "Virtualization/vmm/HypervisorConnector.hpp"
#include "Virtualization/vmm/VirtualMachineConfig.hpp"
#include "Utils/Result.hpp"

class VirtualMachineFactory
{
public:
    explicit VirtualMachineFactory(std::shared_ptr<HypervisorConnector> conn);
    ~VirtualMachineFactory();

    // يبني XML من VmConfig (implementation in .cpp)
    [[nodiscard]] Result<std::string> buildDomainXML(const VmConfig& cfg);

    // يعرّف domain في libvirt ويعيد المقبض (implementation in .cpp)
    [[nodiscard]] Result<virDomainPtr> defineDomain(const std::string& xml);

private:
    std::shared_ptr<HypervisorConnector> connector;
};
    [[nodiscard]] Result<virDomainPtr> defineDomain(const std::string& xml);
};
