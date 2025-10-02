#include "/home/hussin/Desktop/PenHive/include/Virtualization/vmm/VirtualMachineConfig.hpp"
#include <sstream>

bool VmConfig::validate() const {
    if (name.empty()) return false;
    if (memory == 0 || vcpus == 0) return false;
    if (disks.empty()) return false;
    return true;
}

std::string VmConfig::toXML() const {
    std::ostringstream xml;
    xml << "<domain type='kvm'>"
        << "<name>" << name << "</name>"
        << "<memory unit='KiB'>" << memory << "</memory>"
        << "<vcpu>" << vcpus << "</vcpu>"
        << "<os><type arch='" << arch << "'>" << osType << "</type></os>";
    xml << "<devices>";
    if (!disks.empty()) {
        const auto& d = disks.front();
        xml << "<disk type='" << d.type << "' device='" << d.device << "'>"
            << "<driver type='" << d.driver << "'/>"
            << "<source file='" << d.source << "'/>"
            << "<target dev='" << d.target << "'/>"
            << "</disk>";
    }
    xml << "</devices></domain>";
    return xml.str();
}

VmConfig VmConfig::fromXML(const std::string& /*xml*/) {
    VmConfig cfg;
    // TODO: parse using pugixml for robust parsing
    return cfg;
}
