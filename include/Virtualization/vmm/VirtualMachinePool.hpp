#pragma once 

#include "Core/interfaces/IDatabase.hpp"
#include "Utils/Result.hpp"
#include <uuid/uuid.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio.hpp>
#include <string>
#include <map>
#include <memory>
#include "Virtualization/vmm/HypervisorConnector.hpp"

class VirtualMachinePool : public IRocksDB
{
public:
    explicit VirtualMachinePool(std::shared_ptr<HypervisorConnector> connector);
    ~VirtualMachinePool();

    /**
     * Allocate a new VM record and persist minimal metadata.
     * Returns Result<int> where int is an internal oid (simulated).
     */
    [[nodiscard]] Result<int> allocate();

    // Simple getters/setters for pool-managed meta (skeleton)
    void get(int vID);
    void set();

private:
    [[nodiscard]] std::string generate_uuid() {
        using namespace boost;
        uuids::random_generator gen;
        uuids::uuid u = gen();
        return uuids::to_string(u);
    }

    [[nodiscard]] int findAvailablePort(
        int startPort = 5900,
        int endPort = 6000);

    std::shared_ptr<HypervisorConnector> connector;
    std::shared_ptr<IRocksDB> db;
    int nextId{1}; // simple in-memory id generator for demo
};
