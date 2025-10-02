#include "/home/hussin/Desktop/PenHive/include/Virtualization/vmm/HypervisorConnector.hpp"
#include <stdexcept>
#include <libvirt/libvirt.h>

HypervisorConnector::HypervisorConnector(std::shared_ptr<IRocksDB> db)
    : db(std::move(db)), conn(nullptr) {}

HypervisorConnector::~HypervisorConnector() {
    close();
}

bool HypervisorConnector::connect(const std::string& uri) noexcept {
    std::scoped_lock lock(mutex_);
    if (conn) return true;
    conn = virConnectOpen(uri.c_str());
    return conn != nullptr;
}

void HypervisorConnector::connectOrThrow(const std::string& uri) {
    if (!connect(uri)) {
        virErrorPtr e = virGetLastError();
        throw std::runtime_error(std::string("libvirt: connect failed: ") + (e && e->message ? e->message : "unknown"));
    }
}

void HypervisorConnector::close() noexcept {
    std::scoped_lock lock(mutex_);
    if (conn) {
        virConnectClose(conn);
        conn = nullptr;
    }
}

virConnectPtr HypervisorConnector::getRawHandle() const noexcept {
    return conn;
}

virConnectPtr HypervisorConnector::ensureConnected() {
    std::scoped_lock lock(mutex_);
    if (!conn) {
        lock.unlock();
        connectOrThrow();
        lock.lock();
    }
    return conn;
}

bool HypervisorConnector::isConnected() const noexcept {
    return conn != nullptr;
}

std::shared_ptr<IRocksDB> HypervisorConnector::getDB() const noexcept {
    return db;
}
