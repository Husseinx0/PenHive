#pragma once
#include <string>
#include <memory>
#include <string_view>
#include "Virtualization/vm/VirtualMachinePool.hpp"
#include "Virtualization/vm/VirtualMachine.hpp"
#include "Utils/Result.hpp"

class VirtualMachineManager {
    std::shared_ptr<class HypervisorConnector> connector;

public:
    explicit VirtualMachineManager(std::shared_ptr<HypervisorConnector> conn);
    void dispatch_deploy(int vID);
    //[[nodiscard]] Result<std::unique_ptr<VirtualMachine>> createDomain(const VolumeSpec& spec);
    [[nodiscard]] Result<std::unique_ptr<VirtualMachine>> findDomainByName(std::string_view name);
    [[nodiscard]] Result<std::vector<std::unique_ptr<VirtualMachine>>> listAllDomains();
    [[nodiscard]] Result<void> deleteDomain(std::string_view name, bool deleteStorage = false);
private:
    VirtualMachinePool *vmpool;
};
/*
// src/hypervisor/VirtualMachineManager.cpp
#include "virtualization/VirtualMachineManager.hpp"
#include "virtualization/VirtualMachine.hpp"
#include "System/Logger.hpp"
#include "virtualization/builder/DomainDefinitionBuilder.hpp"
#include "System/Logger.hpp"
#include <libvirt/libvirt.h>
:
VirtualMachineManager::VirtualMachineManager(std::shared_ptr<HypervisorConnector> conn)
    : connector(std::move(conn)) {}
void VirtualMachineManager::dispatch_deploy
Result<std::unique_ptr<VirtualMachine>> VirtualMachineManager::createDomain(const VolumeSpec& spec) {
    // توليد XML باستخدام XmlGenerator المحسن
    XmlGenerator xml;
    xml.beginElement("domain").attribute("type", "kvm")
        .beginElement("name").text(spec.domainName).endElement()
        .beginElement("memory").attribute("unit", "MiB").text(std::to_string(spec.memoryMB)).endElement()
        .beginElement("vcpu").text(std::to_string(spec.vcpuCount)).endElement()
        .beginElement("os")
            .beginElement("type").attribute("arch", spec.arch).attribute("machine", "pc").text(spec.osType).endElement()
            .beginElement("boot").attribute("dev", "hd").endElement()
        .endElement()
        .beginElement("devices")
            .beginElement("emulator").text("/usr/bin/qemu-system-x86_64").endElement()
            .beginElement("disk").attribute("type", "file").attribute("device", "disk")
                .beginElement("driver").attribute("name", "qemu").attribute("type", "qcow2").endElement()
                .beginElement("source").attribute("file", spec.diskPath).endElement()
                .beginElement("target").attribute("dev", "vda").attribute("bus", "virtio").endElement()
            .endElement()
            .beginElement("interface").attribute("type", "network")
                .beginElement("source").attribute("network", "default").endElement()
                .beginElement("model").attribute("type", "virtio").endElement()
            .endElement()
            .beginElement("graphics").attribute("type", "spice").attribute("autoport", "yes").endElement()
        .endElement()
    .endElement();

    VLOG_DEBUG("Defining domain with XML:\n{}", xml.toString());

    virDomainPtr domain = virDomainDefineXML(connector->getRawHandle(), xml.toString().c_str());
    if (!domain) {
        return Result<std::unique_ptr<VirtualMachine>>{LibvirtException("Failed to define domain: " + spec.domainName)};
    }

    VLOG_INFO("🆕 Domain defined: {}", spec.domainName);
    return std::make_unique<VirtualMachine>(connector, spec.domainName);
}

Result<std::unique_ptr<VirtualMachine>> VirtualMachineManager::findDomainByName(std::string_view name) {
    virDomainPtr domain = virDomainLookupByName(connector->getRawHandle(), name.data());
    if (!domain) {
        return Result<std::unique_ptr<VirtualMachine>>{VmException("Domain not found: " + std::string(name))};
    }

    return std::make_unique<VirtualMachine>(connector, std::string(name));
}

Result<std::vector<std::unique_ptr<VirtualMachine>>> VirtualMachineManager::listAllDomains() {
    int count = virConnectNumOfDomains(connector->getRawHandle());
    if (count < 0) {
        return Result<std::vector<std::unique_ptr<VirtualMachine>>>{LibvirtException("Failed to get domain count")};
    }

    if (count == 0) return std::vector<std::unique_ptr<VirtualMachine>>{};

    std::vector<int> ids(count);
    int actual = virConnectListDomains(connector->getRawHandle(), ids.data(), count);
    if (actual < 0) {
        return Result<std::vector<std::unique_ptr<VirtualMachine>>>{LibvirtException("Failed to list domains")};
    }

    std::vector<std::unique_ptr<VirtualMachine>> vms;
    vms.reserve(actual);

    for (int i = 0; i < actual; ++i) {
        virDomainPtr domain = virDomainLookupByID(connector->getRawHandle(), ids[i]);
        if (domain) {
            char* name = virDomainGetName(domain);
            if (name) {
                vms.push_back(std::make_unique<VirtualMachine>(connector, name));
                std::free(name);
            }
            virDomainFree(domain);
        }
    }

    return vms;
}

Result<void> VirtualMachineManager::deleteDomain(std::string_view name, bool deleteStorage) {
    auto vmResult = findDomainByName(name);
    if (vmResult.isErr()) {
        return Result<void>{vmResult.unwrapErr()};
    }

    auto& vm = vmResult.unwrap();

    if (vm->isActive()) {
        if (virDomainDestroy(vm->getRawHandle()) < 0) {
            return Result<void>{LibvirtException("Failed to destroy running domain: " + std::string(name))};
        }
    }

    if (virDomainUndefine(vm->getRawHandle()) < 0) {
        return Result<void>{LibvirtException("Failed to undefine domain: " + std::string(name))};
    }

    if (deleteStorage && name.find("-clone-") != std::string::npos) {
        StorageOrchestrator storage(connector);
        std::string volName = std::string(name) + ".qcow2";
        auto deleteResult = storage.deleteVolume(volName);
        if (deleteResult.isErr()) {
            VLOG_WARN("Failed to delete storage for {}: {}", name, deleteResult.unwrapErr());
        }
    }

    VLOG_INFO("🗑️ Domain deleted: {}", name);
    return Result<void>{};
}


*/