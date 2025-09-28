#pragma once

#include "Core/interfaces/IXmlDefinitionBuilderBase.hpp"
#include <memory>
#include <string_view>

/**
 * @brief Concrete builder for domain definition XML documents
 *
 * Implements the IXmlBuilderBase interface to construct libvirt domain XML
 */
class VirtualMachineBuilder : public IXmlBuilderBase {
private:
  // Domain properties
  std::string name;
  std::string uuid;
  unsigned long memoryMiB{ 0 };
  unsigned int vcpuCount{ 0 };
  std::string diskPath;
  std::string osType{ "hvm" };
  std::string architecture{ "x86_64" };
  std::string vncListenAddress{ "127.0.0.1" };
  /**
   * @brief Builds the domain definition XML structure
   *
   * Implements the pure virtual function from IXmlBuilderBase
   * to construct the specific domain XML structure.
   */
  void buildDocument() override;

  // Helper methods for building specific sections
  void buildOsSection();
  void buildMemorySection();
  void buildCpuSection();
  void buildDiskSection();
  void buildDevicesSection();
  void bildGraphicsSection();

public:
  VirtualMachineBuilder() = default;
  ~VirtualMachineBuilder() override = default;

  // Builder methods with fluent interface
  VirtualMachineBuilder& setName(std::string_view name);
  VirtualMachineBuilder& setMemoryMiB(unsigned long memory);
  VirtualMachineBuilder& setCpuCount(unsigned int vcpus);
  VirtualMachineBuilder& setDisk(std::string_view diskPath);
  VirtualMachineBuilder& setOsType(std::string_view osType = "hvm");
  VirtualMachineBuilder& setArchitecture(std::string_view arch = "x86_64");

  /**
   * @brief Builds and returns the formatted XML document
   *
   * Overrides the base build method if needed, otherwise inherits
   * the standard implementation from IXmlBuilderBase
   */
  [[nodiscard]] std::string build() { return IXmlBuilderBase::build(); }

  /**
   * @brief Resets the builder to initial state
   *
   * Clears all domain properties and resets the XML document
   */
  void reset() noexcept {
    IXmlBuilderBase::reset();
    name.clear();
    memoryMiB = 0;
    vcpuCount = 0;
    diskPath.clear();
    osType = "hvm";
    architecture = "x86_64";
  }
};