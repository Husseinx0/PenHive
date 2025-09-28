#include "Virtualization/builder/VirtualMachineNicBuilder.hpp"
#include <pugixml.hpp>

void VirtualMachineNicBuilder::buildDocument() {
    auto interface = doc.append_child("interface");
    interface.append_attribute("type") = deviceType.c_str();
    
    auto mac = interface.append_child("mac");
    if (!macAddress.empty()) {
        mac.append_attribute("address") = macAddress.c_str();
    }
    
    auto source = interface.append_child("source");
    if (deviceType == "network") {
        source.append_attribute("network") = networkName.c_str();
    } else if (deviceType == "bridge") {
        source.append_attribute("bridge") = sourceDevice.c_str();
    } else if (deviceType == "direct") {
        source.append_attribute("dev") = sourceDevice.c_str();
        source.append_attribute("mode") = "passthrough";
    }
    
    auto modelNode = interface.append_child("model");
    modelNode.append_attribute("type") = model.c_str();
}

// Fluent interface implementations
VirtualMachineNicBuilder& VirtualMachineNicBuilder::setModel(std::string_view model) {
    this->model = model;
    return *this;
}

VirtualMachineNicBuilder& VirtualMachineNicBuilder::setMacAddress(std::string_view mac) {
    this->macAddress = mac;
    return *this;
}

VirtualMachineNicBuilder& VirtualMachineNicBuilder::setNetworkName(std::string_view network) {
    this->networkName = network;
    return *this;
}

VirtualMachineNicBuilder& VirtualMachineNicBuilder::setDeviceType(std::string_view type) {
    this->deviceType = type;
    return *this;
}

VirtualMachineNicBuilder& VirtualMachineNicBuilder::setSourceDevice(std::string_view device) {
    this->sourceDevice = device;
    return *this;
}