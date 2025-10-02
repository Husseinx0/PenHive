#pragma once

#include "Core/interfaces/IDatabase.hpp"
#include <libvirt/libvirt.h>
#include <memory>
#include <string>
#include <mutex>

class HypervisorConnector {
public:
    HypervisorConnector() = delete;
    explicit HypervisorConnector(std::shared_ptr<IRocksDB> db);
    ~HypervisorConnector();

    HypervisorConnector(const HypervisorConnector&) = delete;
    HypervisorConnector& operator=(const HypervisorConnector&) = delete;

    // اتصال/إدارة مقبض libvirt
    bool connect(const std::string& uri = "qemu:///system") noexcept;
    void connectOrThrow(const std::string& uri = "qemu:///system");
    void close() noexcept;

    [[nodiscard]] virConnectPtr getRawHandle() const noexcept;
    [[nodiscard]] virConnectPtr ensureConnected();
    [[nodiscard]] bool isConnected() const noexcept;
    [[nodiscard]] std::shared_ptr<IRocksDB> getDB() const noexcept;

private:
    mutable std::mutex mutex_;
    virConnectPtr conn{nullptr};
    std::shared_ptr<IRocksDB> db;
};
			virErrorPtr e = virGetLastError();
			throw std::runtime_error(std::string("libvirt: connect failed: ") + (e && e->message ? e->message : "unknown"));
		}
	}

	// إغلاق الاتصال (آمن متزامنًا)
	void close() noexcept {
		std::scoped_lock lock(mutex_);
		if (conn) {
			virConnectClose(conn);
			conn = nullptr;
		}
	}

	// الحصول على المقبض الخام لاستخدام libvirt؛ يعيد nullptr اذا لم يكن متصلًا
	[[nodiscard]] virConnectPtr getRawHandle() const noexcept {
		return conn;
	}

	// ضمان الاتصال وإرجاع المقبض أو رمي استثناء
	[[nodiscard]] virConnectPtr ensureConnected() {
		std::scoped_lock lock(mutex_);
		if (!conn) {
			lock.unlock();
			connectOrThrow();
			lock.lock();
		}
		return conn;
	}

	[[nodiscard]] bool isConnected() const noexcept {
		return conn != nullptr;
	}

	[[nodiscard]] std::shared_ptr<IRocksDB> getDB() const noexcept {
		return db;
	}

private:
	mutable std::mutex mutex_;
	virConnectPtr conn{nullptr};
	std::shared_ptr<IRocksDB> db;
};
