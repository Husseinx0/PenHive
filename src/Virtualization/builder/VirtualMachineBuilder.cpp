#include "Virtualization/builder/VirtualMachineBuilder.hpp"
#include <pugixml.hpp>

void VirtualMachineBuilder::buildDocument() {
  auto root = doc.append_child("domain");
  root.append_attribute("type") = "kvm";
  if (!name.empty()) {
    root.append_child("name").text() = name.c_str();
  }

  if (!uuid.empty()) {
    root.append_child("uuid").text() = uuid.c_str();
  } else {
    // توليد uuid تلقائياً إذا لم يتم تحديده
   // root.append_child("uuid").text() = generateUuid().c_str();
  }
  // Build all sections
  buildOsSection();
  buildMemorySection();
  buildCpuSection();
  buildDevicesSection();
}

void VirtualMachineBuilder::buildOsSection() {
  auto os = doc.child("domain").append_child("os");
  auto type = os.append_child("type");
  type.append_attribute("arch") = architecture.c_str();
  type.text() = osType.c_str();
  os.append_child("boot").append_attribute("dev") = "hd";
}

void VirtualMachineBuilder::buildMemorySection() {
  auto memory = doc.child("domain").append_child("memory");
  memory.append_attribute("unit") = "MiB";
  memory.text() = memoryMiB;
}

void VirtualMachineBuilder::buildCpuSection() {
  auto vcpu = doc.child("domain").append_child("vcpu");
  vcpu.text() = vcpuCount;
}

void VirtualMachineBuilder::buildDevicesSection() {
  auto devices = doc.child("domain").append_child("devices");

  // Disk device
  if (!diskPath.empty()) {
    auto disk = devices.append_child("disk");
    disk.append_attribute("type") = "file";
    disk.append_attribute("device") = "disk";

    auto driver = disk.append_child("driver");
    driver.append_attribute("name") = "qemu";
    driver.append_attribute("type") = "qcow2";

    auto source = disk.append_child("source");
    source.append_attribute("file") = diskPath.c_str();

    disk.append_child("target").append_attribute("dev") = "vda";
  }

  // Additional standard devices
  devices.append_child("emulator").text() = "/usr/bin/qemu-system-x86_64";
}

void VirtualMachineBuilder::bildGraphicsSection() {
  auto devices = doc.child("domain").child("devices");
  auto graphics = devices.append_child("graphics");

  graphics.append_attribute("type") = "spice";
  graphics.append_attribute("port") = "-1";
  graphics.append_attribute("autoport") = "yes";
  graphics.append_attribute("listen") = vncListenAddress.c_str();

  auto listen = graphics.append_child("listen");
  listen.append_attribute("type") = "address";
  listen.append_attribute("address") = vncListenAddress.c_str();
}
// Fluent interface implementations
VirtualMachineBuilder& VirtualMachineBuilder::setName(std::string_view name) {
  this->name = name;
  return *this;
}

VirtualMachineBuilder& VirtualMachineBuilder::setMemoryMiB(unsigned long memory) {
  this->memoryMiB = memory;
  return *this;
}

VirtualMachineBuilder& VirtualMachineBuilder::setCpuCount(unsigned int vcpus) {
  this->vcpuCount = vcpus;
  return *this;
}

VirtualMachineBuilder& VirtualMachineBuilder::setDisk(std::string_view diskPath) {
  this->diskPath = diskPath;
  return *this;
}

VirtualMachineBuilder& VirtualMachineBuilder::setOsType(std::string_view osType) {
  this->osType = osType;
  return *this;
}

VirtualMachineBuilder& VirtualMachineBuilder::setArchitecture(std::string_view arch) {
  this->architecture = arch;
  return *this;
}