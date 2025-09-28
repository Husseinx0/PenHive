#pragma once

#include "Core/interfaces/IXmlDefinitionBuilderBase.hpp"
#include <string_view>

/**
 * @brief Builder for virtual machine network interface (NIC) configuration
 * 
 * Constructs libvirt NIC XML devices using fluent interface pattern
 */
class VirtualMachineNicBuilder : public IXmlBuilderBase {
private:
    /**
     * @brief Builds the network interface XML structure
     * 
     * Implements the pure virtual function from IXmlBuilderBase
     * to construct the specific NIC device XML structure.
     */
    void buildDocument() override;

    // NIC configuration properties
    std::string model{"virtio"};
    std::string macAddress;
    std::string networkName{"default"};
    std::string deviceType{"network"};
    std::string sourceDevice;

public:
    VirtualMachineNicBuilder() = default;
    ~VirtualMachineNicBuilder() override = default;

    /**
     * @brief Sets the NIC device model
     * @param model Device model (e.g., "virtio", "e1000")
     */
    VirtualMachineNicBuilder& setModel(std::string_view model);
    
    /**
     * @brief Sets the MAC address for the NIC
     * @param mac MAC address in format "XX:XX:XX:XX:XX:XX"
     */
    VirtualMachineNicBuilder& setMacAddress(std::string_view mac);
    
    /**
     * @brief Sets the network name for the NIC
     * @param network Name of the virtual network
     */
    VirtualMachineNicBuilder& setNetworkName(std::string_view network);
    
    /**
     * @brief Sets the device type
     * @param type Device type ("network", "bridge", "direct")
     */
    VirtualMachineNicBuilder& setDeviceType(std::string_view type);
    
    /**
     * @brief Sets the source device for bridge/direct types
     * @param device Source device name
     */
    VirtualMachineNicBuilder& setSourceDevice(std::string_view device);

    /**
     * @brief Builds and returns the formatted XML document
     * @return Formatted XML string representation
     */
    [[nodiscard]] std::string build() {
        return IXmlBuilderBase::build();
    }

    /**
     * @brief Resets the builder to initial state
     * 
     * Clears all configuration and prepares for new build
     */
    void reset() noexcept {
        IXmlBuilderBase::reset();
        model = "virtio";
        macAddress.clear();
        networkName = "default";
        deviceType = "network";
        sourceDevice.clear();
    }
};