#pragma once
#include <libvirt/libvirt.h>
#include <string>

class VirtualMachineNic {
public:
    VirtualMachineNic();
    explicit VirtualMachineNic(std::string mac);
    ~VirtualMachineNic();

    bool attach(virDomainPtr domain);
    bool detach(virDomainPtr domain);
    [[nodiscard]] std::string getMac() const noexcept;

private:
    std::string mac;
    std::string generate_mac();
};