#include "/home/hussin/Desktop/PenHive/include/Virtualization/vmm/VirtualMachineFactory.hpp"
#include <sstream>
#include <libvirt/libvirt.h>

VirtualMachineFactory::VirtualMachineFactory(std::shared_ptr<HypervisorConnector> conn)
    : connector(std::move(conn)) {}
VirtualMachineFactory::~VirtualMachineFactory() = default;

Result<std::string> VirtualMachineFactory::buildDomainXML(const VmConfig& cfg) {
    if (!cfg.validate()) return Result<std::string>{std::string("Invalid VM config")};
    std::ostringstream xml;
    xml << "<domain type='kvm'>"
        << "<name>" << cfg.name << "</name>"
        << "<memory unit='KiB'>" << cfg.memory << "</memory>"
        << "<vcpu>" << cfg.vcpus << "</vcpu>"
        << "<os><type arch='" << cfg.arch << "'>" << cfg.osType << "</type></os>"
        << "<devices>";
    if (!cfg.disks.empty()) {
        const auto& d = cfg.disks[0];
        xml << "<disk type='" << d.type << "' device='" << d.device << "'>"
            << "<driver type='" << d.driver << "'/>"
            << "<source file='" << d.source << "'/>"
            << "<target dev='" << d.target << "'/>"
            << "</disk>";
    }
    xml << "<interface type='network'><source network='default'/></interface>";
    xml << "</devices></domain>";
    return Result<std::string>{xml.str()};
}

Result<virDomainPtr> VirtualMachineFactory::defineDomain(const std::string& xml) {
    if (!connector) return Result<virDomainPtr>{std::string("No connector")};
    virConnectPtr c = connector->getRawHandle();
    if (!c) return Result<virDomainPtr>{std::string("Not connected")};
    virDomainPtr dom = virDomainDefineXML(c, xml.c_str());
    if (!dom) {
        virErrorPtr err = virGetLastError();
        return Result<virDomainPtr>{std::string(std::string("virDomainDefineXML failed: ") + (err && err->message ? err->message : "unknown"))};
    }
    return Result<virDomainPtr>{dom};
}
