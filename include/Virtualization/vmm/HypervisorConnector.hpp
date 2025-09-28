#pragma once

#include "Core/interfaces/IDatabase.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <uuid/uuid.h>

class VirtualMachinePool : public IRocksDB
{
public:
    VirtualMachinePool(/* args */);
    ~VirtualMachinePool();
    /**
     * Function to allocate a new VM object
     *
     *
     * @return 0 in sucuse
     */
    [[nodiscard]] int allocate(int data, string name);
    void set(int data, string name);

private:
    [[nodiscard]] std::string generate_uuid()
    {
        using namespace boost;
        uuids::random_generator gen;
        uuids::uuid u = gen();
        return uuids::to_string(u);
    }
};
