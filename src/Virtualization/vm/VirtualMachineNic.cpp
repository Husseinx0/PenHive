#include "/home/hussin/Desktop/PenHive/include/Virtualization/vm/VirtualMachineNic.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>

VirtualMachineNic::VirtualMachineNic() : mac(generate_mac()) {}
VirtualMachineNic::VirtualMachineNic(std::string m) : mac(std::move(m)) {}
VirtualMachineNic::~VirtualMachineNic() = default;

bool VirtualMachineNic::attach(virDomainPtr domain) {
    if (!domain) throw std::invalid_argument("domain is null");
    std::ostringstream xml;
    xml << "<interface type='network'>"
        << "<source network='default'/>"
        << "<model type='virtio'/>"
        << "<mac address='" << mac << "'/>"
        << "</interface>";
    int rc = virDomainAttachDeviceFlags(domain, xml.str().c_str(), VIR_DOMAIN_AFFECT_CONFIG | VIR_DOMAIN_AFFECT_LIVE);
    return rc == 0;
}

bool VirtualMachineNic::detach(virDomainPtr domain) {
    if (!domain) throw std::invalid_argument("domain is null");
    std::ostringstream xml;
    xml << "<interface type='network'>"
        << "<source network='default'/>"
        << "<model type='virtio'/>"
        << "<mac address='" << mac << "'/>"
        << "</interface>";
    int rc = virDomainDetachDeviceFlags(domain, xml.str().c_str(), VIR_DOMAIN_AFFECT_CONFIG | VIR_DOMAIN_AFFECT_LIVE);
    return rc == 0;
}

std::string VirtualMachineNic::getMac() const noexcept { return mac; }

std::string VirtualMachineNic::generate_mac() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 6; ++i) {
        int byte = dis(gen);
        if (i == 0) byte = (byte & 0xFE) | 0x02;
        ss << std::setw(2) << (byte & 0xFF);
        if (i != 5) ss << ":";
    }
    return ss.str();
}