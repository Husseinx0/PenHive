#include "Virtualization/vm/VirtualMachine.hpp"
std::unique_ptr<virDomainPtr> VirtualMachine::getDomain(){
        return std::unique_ptr<virDomain>(domain);
}