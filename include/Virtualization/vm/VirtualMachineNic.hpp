#pragma once
#include "Virtualization/builder/VirtualMachineNicBuilder.hpp"
#include <libvirt/libvirt.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
class VirtualMachineNic {
private:
  /*
      const char* xml = R"(
  <interface type='bridge'>
      <source bridge='br0'/>
      <target dev='mytap0'/>
      <model type='virtio'/>
  </interface>
)";

virDomainAttachDeviceFlags(vm, xml, VIR_DOMAIN_AFFECT_CONFIG | VIR_DOMAIN_AFFECT_LIVE);
  */

public:
  VirtualMachineNic(/* args */);
  ~VirtualMachineNic();
  
  // إرفاق البطاقة بآلة افتراضية
  bool attach(virDomainPtr domain);

  // فصل البطاقة عن آلة افتراضية
  bool detach(virDomainPtr domain);

private:
  enum class State { ATTACHED, DETACHED };
  std::string mac;

  std::string generate_mac();
};


#include <libvirt/virterror.h>
#include <sstream>

LibvirtVMNic::LibvirtVMNic(const std::string& mac, const std::string& networkName, NetworkType type)
    : mac(mac)
    , networkName(networkName)
    , networkType(type)
    , currentState(State::DETACHED) {
  if (mac.empty()) {
    // توليد MAC عشوائي إذا لم يتم تقديمه
    this->mac = generate_random_mac();
  }
}

std::string LibvirtVMNic::get_id() const { return mac; }

std::string LibvirtVMNic::get_mac() const { return mac; }

std::string LibvirtVMNic::get_network_name() const { return networkName; }

LibvirtVMNic::NetworkType LibvirtVMNic::get_network_type() const { return networkType; }

void LibvirtVMNic::set_ip_address(const std::string& ip) { ipAddress = ip; }

std::string LibvirtVMNic::get_ip_address() const { return ipAddress; }

bool LibvirtVMNic::attach(virDomainPtr domain) {
  std::string xml = generate_xml();
  if (xml.empty()) {
    return false;
  }

  // إرفاق الجهاز بالdomain
  if (virDomainAttachDeviceFlags(domain, xml.c_str(), VIR_DOMAIN_DEVICE_MODIFY_CONFIG) == 0) {
    currentState = State::ATTACHED;
    return true;
  } else {
    // معالجة الخطأ
    virErrorPtr err = virGetLastError();
    throw std::runtime_error("Failed to attach NIC: " + std::string(err->message));
  }
}

bool LibvirtVMNic::detach(virDomainPtr domain) {
  std::string xml = generate_xml();
  if (xml.empty()) {
    return false;
  }

  if (virDomainDetachDeviceFlags(domain, xml.c_str(), VIR_DOMAIN_DEVICE_MODIFY_CONFIG) == 0) {
    currentState = State::DETACHED;
    return true;
  } else {
    virErrorPtr err = virGetLastError();
    throw std::runtime_error("Failed to detach NIC: " + std::string(err->message));
  }
}

LibvirtVMNic::State LibvirtVMNic::get_state() const { return currentState; }

std::string LibvirtVMNic::generate_xml() const {
  std::ostringstream oss;
  oss << "<interface type='";

  switch (networkType) {
    case NetworkType::BRIDGE:
      oss << "bridge'>\n";
      oss << "  <source bridge='" << networkName << "'/>\n";
      break;
    case NetworkType::NETWORK:
      oss << "network'>\n";
      oss << "  <source network='" << networkName << "'/>\n";
      break;
    case NetworkType::DIRECT:
      oss << "direct'>\n";
      oss << "  <source dev='" << networkName << "' mode='bridge'/>\n";
      break;
    case NetworkType::USER:
      oss << "user'>\n";
      break;
    default:
      return ""; // نوع غير معروف
  }

  oss << "  <mac address='" << mac << "'/>\n";

  if (!ipAddress.empty()) {
    oss << "  <ip address='" << ipAddress << "'/>\n";
  }

  if (!securityGroups.empty()) {
    oss << "  <virtualport type='openvswitch'>\n";
    for (const auto& sg : securityGroups) {
      oss << "    <parameters group='" << sg << "'/>\n";
    }
    oss << "  </virtualport>\n";
  }

  oss << "</interface>";
  return oss.str();
}

void LibvirtVMNic::set_security_groups(const std::set<std::string>& sgs) { securityGroups = sgs; }

std::set<std::string> LibvirtVMNic::get_security_groups() const { return securityGroups; }