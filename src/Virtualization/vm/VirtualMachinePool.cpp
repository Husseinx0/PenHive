#include "/home/hussin/Desktop/PenHive/include/Virtualization/vm/VirtualMachinePool.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

VirtualMachinePool::VirtualMachinePool(std::shared_ptr<HypervisorConnector> connector)
    : connector(std::move(connector)), db(this->connector ? this->connector->getDB() : nullptr) {}

VirtualMachinePool::~VirtualMachinePool() = default;

Result<int> VirtualMachinePool::allocate() {
    std::scoped_lock lock(mutex_);
    int id = nextId++;
    std::string uuid = generate_uuid();
    int reservedPort = -1;
    try { reservedPort = findAvailablePort(5900, 6000); } catch (...) { reservedPort = -1; }
    entries.emplace(id, Entry{uuid, reservedPort});
    // TODO: persist via db if available (use IRocksDB::Put)
    (void)uuid; (void)reservedPort;
    return Result<int>{id};
}

std::optional<std::pair<std::string,int>> VirtualMachinePool::getMeta(int id) {
    std::scoped_lock lock(mutex_);
    auto it = entries.find(id);
    if (it == entries.end()) return std::nullopt;
    return std::make_pair(it->second.uuid, it->second.reservedPort);
}

bool VirtualMachinePool::remove(int id) {
    std::scoped_lock lock(mutex_);
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    entries.erase(it);
    // TODO: remove from db if available
    return true;
}

std::string VirtualMachinePool::generate_uuid() {
    boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

int VirtualMachinePool::findAvailablePort(int startPort, int endPort) {
    if (startPort < 1 || endPort > 65535 || startPort > endPort) {
        throw std::invalid_argument("Invalid port range");
    }
    boost::asio::io_context io_context;
    for (int port = startPort; port <= endPort; ++port) {
        boost::asio::ip::tcp::acceptor acceptor(io_context);
        boost::system::error_code ec;
        acceptor.open(boost::asio::ip::tcp::v4(), ec);
        if (ec) continue;
        acceptor.bind({boost::asio::ip::tcp::v4(), static_cast<unsigned short>(port)}, ec);
        acceptor.close();
        if (!ec) return port;
    }
    throw std::runtime_error("No available port");
}