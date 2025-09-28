#pragma once
#include <libvirt/libvirt.h>
class VirtualMachineFactory
{
private:
    virConnectPtr conn;

public:
    VirtualMachineFactory(/* args */);
    ~VirtualMachineFactory();
};

VirtualMachineFactory::VirtualMachineFactory(/* args */)
{
}

VirtualMachineFactory::~VirtualMachineFactory()
{
}
