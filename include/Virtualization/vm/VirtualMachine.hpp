// src/model/VirtualMachine.hpp
#pragma once
#include <string>
#include <memory>
#include <libvirt/libvirt.h>
#include "Virtualization/Utils/VmException.hpp"
#include "Virtualization/vmm/HypervisorConnector.hpp"


using namespace std;
class VirtualMachine {
public:
    enum class VmState { Running, Paused, Shutdown, Crashed, Suspended, Unknown };

    VirtualMachine(std::shared_ptr<HypervisorConnector> conn, std::string_view vmName);
    ~VirtualMachine();

    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;

    [[nodiscard]] int setDomain(virDomainPtr Dom);
    [[nodiscard]] virDomainPtr getDomain();

    void start();
    void shutdown();
    void reboot();
    void destroy();

    [[nodiscard]] const std::string& getName() const noexcept;
    [[nodiscard]] VmState getState() const;
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] virDomainPtr getRawHandle() const noexcept;

private:
    std::shared_ptr<HypervisorConnector> connector;
    virDomainPtr domain {nullptr};
    std::string name;
    VmState state;

    void refreshHandle();
    void checkLibvirtError(int result, const std::string& action);
    static VmState mapLibvirtState(int state);

    int bootstrap();
};