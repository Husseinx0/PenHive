#pragma once
#include <string>
#include <memory>
#include <string_view>
#include <vector>
#include <mutex>
#include <functional>
#include <atomic>
#include <chrono>
#include "Virtualization/vm/VirtualMachinePool.hpp"
#include "Virtualization/vm/VirtualMachine.hpp"
#include "Utils/Result.hpp"
#include "Virtualization/vmm/HypervisorConnector.hpp"
#include "Virtualization/vmm/VirtualMachineFactory.hpp"
#include "Virtualization/vmm/VirtualMachineDriver.hpp"
#include "Virtualization/vmm/VirtualMachineConfig.hpp"
#include "Core/concurrency/EventDispatcher.hpp"
#include "Utils/Logger.hpp"

class VirtualMachineManager {
public:
    // ctor: optional injected dispatcher (if null, manager creates its own)
    explicit VirtualMachineManager(std::shared_ptr<HypervisorConnector> conn,
                                   std::shared_ptr<CONCURRENCY::EventDispatcher> dispatcher = nullptr);
    ~VirtualMachineManager();

    // synchronous deploy (blocking)
    [[nodiscard]] Result<int> dispatch_deploy(const VmConfig& cfg);

    // asynchronous deploy: schedules deploy on dispatcher, optional callback called with Result<int>
    void dispatch_deploy_async(const VmConfig& cfg, std::function<void(Result<int>)> callback = nullptr);

    // عمليات قراءة / حذف
    [[nodiscard]] Result<std::unique_ptr<VirtualMachine>> findDomainByName(std::string_view name);
    [[nodiscard]] Result<std::vector<std::unique_ptr<VirtualMachine>>> listAllDomains();
    [[nodiscard]] Result<void> deleteDomain(std::string_view name, bool deleteStorage = false);

    // جدولة فحص حالة دورية للـ VM. تعيد flag للإلغاء (عند وضع true يتم إيقاف الفحص)
    [[nodiscard]] std::shared_ptr<std::atomic<bool>> schedule_health_check(std::string vmName, std::chrono::seconds interval);

private:
    std::shared_ptr<HypervisorConnector> connector;
    std::unique_ptr<VirtualMachinePool> vmpool;
    std::unique_ptr<VirtualMachineFactory> factory;
    std::unique_ptr<VirtualMachineDriver> driver;

    // Dispatcher للمهام الخلفية
    std::shared_ptr<CONCURRENCY::EventDispatcher> dispatcher_;
    bool own_dispatcher_{false};

    std::mutex managerMutex;
};