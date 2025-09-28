#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <format>
#include <atomic>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <functional>
#include <span>
#include <ranges>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <map>
#include <unordered_map>
#include <semaphore>
#include <latch>
#include <barrier>
#include <stop_token>
#include <variant>
#include <expected>
#include <source_location>
#include <print>
#include <curses.h>
#include <menu.h>
#include <panel.h>
#include <random>
#include <csignal>
#include <sys/statvfs.h>
#include <cstring>
#include <cstdlib>
#include <form.h>
#include <sstream>
#include <iomanip>
#include <optional>
#include <regex>

namespace fs = std::filesystem;
namespace rng = std::ranges;
namespace chrono = std::chrono;

using namespace std::chrono_literals;

// ===== الأنواع والأحداث =====
enum class VMStatus { Stopped, Running, Paused, Error, Creating, Migrating, Suspended };
enum class ResourceType { CPU, Memory, IO, Network };
enum class ScalingAction { ScaleUp, ScaleDown, Maintain, Migrate, Suspend, Resume };

struct ResourceUsage {
    double cpu_percent{0.0};
    uint64_t memory_bytes{0};
    uint64_t memory_max_bytes{0};
    uint64_t io_read_bps{0};
    uint64_t io_write_bps{0};
    uint64_t network_rx_bps{0};
    uint64_t network_tx_bps{0};
    chrono::system_clock::time_point timestamp;
};

struct ResourceLimit {
    ResourceType type;
    uint64_t min_value;
    uint64_t max_value;
    uint64_t current_value;
    std::string unit;
};

struct VMMetrics {
    std::string vm_name;
    ResourceUsage usage;
    std::vector<double> cpu_history;
    std::vector<uint64_t> memory_history;
    double cpu_avg_5min{0.0};
    double cpu_avg_15min{0.0};
    double memory_avg_5min{0.0};
};

struct ScalingDecision {
    ScalingAction action;
    ResourceType resource;
    uint64_t amount;
    std::string vm_name;
    chrono::system_clock::time_point timestamp;
    double confidence{0.0};
    std::string reason;
};

struct HostMetrics {
    uint64_t total_memory{0};
    uint64_t free_memory{0};
    uint64_t available_memory{0};
    double cpu_load_1min{0.0};
    double cpu_load_5min{0.0};
    double cpu_load_15min{0.0};
    uint64_t io_throughput{0};
    uint64_t network_throughput{0};
    uint64_t disk_usage_percent{0};
};

struct VMConfig {
    std::string name;
    std::vector<ResourceLimit> limits;
    std::string image_path;
    uint16_t vcpus{2};
    uint64_t memory_mb{2048};
    std::string os_type{"linux"};
    std::string arch{"x86_64"};
    std::string network_bridge{"virbr0"};
    std::string video_model{"virtio"};
    uint16_t video_vram{16384};
};

struct VMSnapshot {
    std::string name;
    std::string description;
    chrono::system_clock::time_point created_at;
    std::string parent_snapshot;
    uint64_t disk_size{0};
    VMStatus vm_state;
};

// ===== استثناءات مخصصة =====
class VirtualizationException : public std::runtime_error {
public:
    VirtualizationException(const std::string& msg, 
                          std::source_location loc = std::source_location::current())
        : std::runtime_error(std::format("{} at {}:{}", msg, loc.file_name(), loc.line())) {}
};

class LibvirtException : public VirtualizationException {
public:
    LibvirtException(const std::string& msg, int code,
                   std::source_location loc = std::source_location::current())
        : VirtualizationException(std::format("Libvirt error {}: {}", code, msg), loc) {}
};

class CGroupException : public VirtualizationException {
public:
    CGroupException(const std::string& msg,
                  std::source_location loc = std::source_location::current())
        : VirtualizationException(std::format("CGroup error: {}", msg), loc) {}
};

class VMOperationException : public VirtualizationException {
public:
    VMOperationException(const std::string& vm_name, const std::string& operation,
                       std::source_location loc = std::source_location::current())
        : VirtualizationException(std::format("Failed to {} VM {}", operation, vm_name), loc) {}
};

// ===== مدير اتصال Libvirt =====
class LibvirtConnection {
private:
    virConnectPtr conn{nullptr};
    std::string uri;
    
public:
    explicit LibvirtConnection(const std::string& hypervisor_uri = "qemu:///system") 
        : uri(hypervisor_uri) {
        conn = virConnectOpen(uri.c_str());
        if (!conn) {
            throw LibvirtException("Failed to connect to libvirt", -1);
        }
        std::println("✅ Libvirt connection established to: {}", uri);
    }
    
    ~LibvirtConnection() {
        if (conn) {
            virConnectClose(conn);
            std::println("🔌 Libvirt connection closed: {}", uri);
        }
    }
    
    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;
    
    LibvirtConnection(LibvirtConnection&& other) noexcept : conn(other.conn), uri(std::move(other.uri)) {
        other.conn = nullptr;
    }
    
    LibvirtConnection& operator=(LibvirtConnection&& other) noexcept {
        if (this != &other) {
            if (conn) virConnectClose(conn);
            conn = other.conn;
            uri = std::move(other.uri);
            other.conn = nullptr;
        }
        return *this;
    }
    
    virConnectPtr get() const { return conn; }
    const std::string& getUri() const { return uri; }
    
    std::string getHypervisorVersion() const {
        unsigned long version;
        if (virConnectGetVersion(conn, &version) == 0) {
            return std::format("{}.{}.{}", 
                version / 1000000, 
                (version % 1000000) / 1000, 
                version % 1000);
        }
        return "Unknown";
    }
    
    std::string getHostname() const {
        char* hostname = virConnectGetHostname(conn);
        if (hostname) {
            std::string result(hostname);
            free(hostname);
            return result;
        }
        return "Unknown";
    }
    
    bool isAlive() const {
        return virConnectIsAlive(conn) == 1;
    }
    
    std::vector<std::string> getStoragePools() const {
        std::vector<std::string> pools;
        char** names = nullptr;
        int count = virConnectListStoragePools(conn, &names);
        
        if (count > 0) {
            for (int i = 0; i < count; i++) {
                pools.emplace_back(names[i]);
                free(names[i]);
            }
            free(names);
        }
        
        return pools;
    }
};

// ===== مدير CGroup =====
class CGroupManager {
private:
    fs::path cgroupPath;
    std::vector<pid_t> managedProcesses;
    std::mutex processesMutex;
    std::string cgroupName;
    
public:
    explicit CGroupManager(std::string_view name) 
        : cgroupPath(std::format("/sys/fs/cgroup/{}", name)), cgroupName(name) {
        createCGroup();
    }
    
    ~CGroupManager() {
        try {
            releaseResources();
        } catch (const std::exception& e) {
            std::println("❌ Error in CGroupManager cleanup: {}", e.what());
        }
    }
    
    void setCPULimit(uint64_t quota_us, uint64_t period_us = 100000) {
        writeValue("cpu.max", std::format("{} {}", quota_us, period_us));
    }
    
    void setMemoryLimit(uint64_t limit_bytes) {
        writeValue("memory.max", std::to_string(limit_bytes));
        // Set swap limit to the same as memory to prevent swapping
        writeValue("memory.swap.max", std::to_string(limit_bytes));
    }
    
    void setIOLimit(std::string_view device, uint64_t read_bps, uint64_t write_bps) {
        writeValue("io.max", std::format("{} rbps={} wbps={}", device, read_bps, write_bps));
    }
    
    void setCPUShares(uint64_t shares) {
        writeValue("cpu.shares", std::to_string(shares));
    }
    
    void setMemorySwappiness(uint64_t swappiness) {
        writeValue("memory.swappiness", std::to_string(swappiness));
    }
    
    void addProcess(pid_t pid) {
        writeValue("cgroup.procs", std::to_string(pid));
        
        std::lock_guard lock(processesMutex);
        managedProcesses.push_back(pid);
    }
    
    void removeProcess(pid_t pid) {
        std::lock_guard lock(processesMutex);
        auto it = std::find(managedProcesses.begin(), managedProcesses.end(), pid);
        if (it != managedProcesses.end()) {
            managedProcesses.erase(it);
        }
    }
    
    void releaseResources() {
        std::vector<pid_t> processesCopy;
        {
            std::lock_guard lock(processesMutex);
            processesCopy = managedProcesses;
            managedProcesses.clear();
        }
        
        for (pid_t pid : processesCopy) {
            try {
                removeProcessFromCGroup(pid);
            } catch (const std::exception& e) {
                std::println("❌ Failed to remove process {}: {}", pid, e.what());
            }
        }
        
        try {
            if (fs::exists(cgroupPath) && isCGroupEmpty()) {
                fs::remove_all(cgroupPath);
                std::println("🗑️ Removed CGroup directory: {}", cgroupPath.string());
            }
        } catch (const fs::filesystem_error& e) {
            throw CGroupException(std::format("Failed to remove CGroup: {}", e.what()));
        }
    }
    
    const std::string& getName() const {
        return cgroupName;
    }
    
private:
    void createCGroup() {
        try {
            if (!fs::exists(cgroupPath)) {
                fs::create_directories(cgroupPath);
                std::println("📁 Created CGroup directory: {}", cgroupPath.string());
            }
        } catch (const fs::filesystem_error& e) {
            throw CGroupException(std::format("Failed to create CGroup: {}", e.what()));
        }
    }
    
    void writeValue(std::string_view filename, std::string_view value) {
        std::ofstream file(cgroupPath / filename);
        if (!file) {
            throw CGroupException(std::format("Cannot open file: {}", filename));
        }
        file << value;
    }
    
    void removeProcessFromCGroup(pid_t pid) {
        writeValue("cgroup.procs", std::to_string(pid));
    }
    
    bool isCGroupEmpty() const {
        auto procsFile = cgroupPath / "cgroup.procs";
        if (!fs::exists(procsFile)) return true;
        
        std::ifstream file(procsFile);
        return file.peek() == std::ifstream::traits_type::eof();
    }
};

// ===== الآلة الافتراضية =====
class VirtualMachine {
private:
    std::string name;
    std::string uuid;
    VMStatus status{VMStatus::Stopped};
    VMConfig config;
    virDomainPtr domain{nullptr};
    std::shared_ptr<LibvirtConnection> libvirt_conn;
    std::unique_ptr<CGroupManager> cgroup_manager;
    std::vector<ResourceLimit> resource_limits;
    std::mutex status_mutex;
    std::vector<VMSnapshot> snapshots;
    
public:
    VirtualMachine(std::shared_ptr<LibvirtConnection> conn, const VMConfig& vm_config)
        : libvirt_conn(conn), config(vm_config), name(vm_config.name) {
        
        // إنشاء مدير CGroup لهذه الآلة الافتراضية
        cgroup_manager = std::make_unique<CGroupManager>(std::format("vm_{}", name));
        
        // تعيين حدود الموارد الأولية
        resource_limits = config.limits;
        applyResourceLimits();
        
        std::println("🖥️ Virtual machine object created: {}", name);
    }
    
    ~VirtualMachine() {
        stop();
        std::println("🗑️ Virtual machine destroyed: {}", name);
    }
    
    bool create() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Stopped) {
            std::println("❌ Cannot create VM {}: not in stopped state", name);
            return false;
        }
        
        status = VMStatus::Creating;
        
        try {
            // التحقق من وجود صورة القرص
            if (!fs::exists(config.image_path)) {
                throw VMOperationException(name, "create", std::source_location::current());
            }
            
            // إنشاء نطاق libvirt للآلة الافتراضية
            std::string xml_config = generateDomainXML();
            domain = virDomainDefineXML(libvirt_conn->get(), xml_config.c_str());
            
            if (!domain) {
                throw LibvirtException("Failed to define domain", -1);
            }
            
            // الحصول على UUID الخاص بالآلة
            char uuid_str[VIR_UUID_STRING_BUFLEN];
            if (virDomainGetUUIDString(domain, uuid_str) == 0) {
                uuid = uuid_str;
            }
            
            status = VMStatus::Stopped;
            std::println("✅ VM {} created successfully with UUID: {}", name, uuid);
            return true;
            
        } catch (const std::exception& e) {
            status = VMStatus::Error;
            std::println("❌ Failed to create VM {}: {}", name, e.what());
            return false;
        }
    }
    
    bool start() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Stopped && status != VMStatus::Paused) {
            std::println("❌ Cannot start VM {}: not in stopped or paused state", name);
            return false;
        }
        
        if (!domain) {
            std::println("❌ Cannot start VM {}: domain not defined", name);
            return false;
        }
        
        if (virDomainCreate(domain) != 0) {
            status = VMStatus::Error;
            std::println("❌ Failed to start VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        status = VMStatus::Running;
        std::println("🚀 VM {} started successfully", name);
        
        // تطبيق حدود الموارد بعد التشغيل
        applyResourceLimits();
        
        return true;
    }
    
    bool stop() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running && status != VMStatus::Paused) {
            return true; // Already stopped or in error state
        }
        
        if (!domain) {
            return true; // No domain to stop
        }
        
        if (virDomainDestroy(domain) != 0) {
            std::println("❌ Failed to stop VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        status = VMStatus::Stopped;
        std::println("🛑 VM {} stopped successfully", name);
        return true;
    }
    
    bool shutdown() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running) {
            std::println("❌ Cannot shutdown VM {}: not running", name);
            return false;
        }
        
        if (virDomainShutdown(domain) != 0) {
            std::println("❌ Failed to shutdown VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        status = VMStatus::Stopped;
        std::println("🔌 VM {} shutdown successfully", name);
        return true;
    }
    
    bool pause() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running) {
            std::println("❌ Cannot pause VM {}: not running", name);
            return false;
        }
        
        if (virDomainSuspend(domain) != 0) {
            std::println("❌ Failed to pause VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        status = VMStatus::Paused;
        std::println("⏸️ VM {} paused successfully", name);
        return true;
    }
    
    bool resume() {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Paused) {
            std::println("❌ Cannot resume VM {}: not paused", name);
            return false;
        }
        
        if (virDomainResume(domain) != 0) {
            std::println("❌ Failed to resume VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        status = VMStatus::Running;
        std::println("▶️ VM {} resumed successfully", name);
        return true;
    }
    
    bool restart() {
        if (!stop()) {
            return false;
        }
        
        std::this_thread::sleep_for(2s);
        
        return start();
    }
    
    bool migrate(const std::string& destination_uri) {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running) {
            std::println("❌ Cannot migrate VM {}: not running", name);
            return false;
        }
        
        status = VMStatus::Migrating;
        std::println("🌐 Attempting to migrate VM {} to {}", name, destination_uri);
        
        // تنفيذ الهجرة باستخدام libvirt
        virConnectPtr dest_conn = virConnectOpen(destination_uri.c_str());
        if (!dest_conn) {
            status = VMStatus::Running;
            std::println("❌ Failed to connect to destination: {}", destination_uri);
            return false;
        }
        
        unsigned long flags = VIR_MIGRATE_LIVE | VIR_MIGRATE_UNDEFINE_SOURCE | VIR_MIGRATE_PERSIST_DEST;
        virDomainPtr new_domain = virDomainMigrate(domain, dest_conn, flags, nullptr, nullptr, 0);
        
        if (!new_domain) {
            status = VMStatus::Running;
            virConnectClose(dest_conn);
            std::println("❌ Failed to migrate VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        // تحديث النطاق بعد الهجرة الناجحة
        virDomainFree(domain);
        domain = new_domain;
        virConnectClose(dest_conn);
        
        status = VMStatus::Running;
        std::println("✅ VM {} migrated successfully to {}", name, destination_uri);
        return true;
    }
    
    bool createSnapshot(const std::string& snapshot_name, const std::string& description = "") {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running && status != VMStatus::Paused) {
            std::println("❌ Cannot create snapshot for VM {}: not running or paused", name);
            return false;
        }
        
        // إنشاء XML لـ snapshot
        std::string snapshot_xml = std::format(
            "<domainsnapshot><name>{}</name><description>{}</description></domainsnapshot>",
            snapshot_name, description
        );
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotCreateXML(domain, snapshot_xml.c_str(), 0);
        if (!snapshot) {
            std::println("❌ Failed to create snapshot for VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        // تخزين معلومات الـ snapshot
        VMSnapshot snap_info;
        snap_info.name = snapshot_name;
        snap_info.description = description;
        snap_info.created_at = chrono::system_clock::now();
        snap_info.vm_state = status;
        
        snapshots.push_back(snap_info);
        
        virDomainSnapshotFree(snapshot);
        
        std::println("📸 Created snapshot '{}' for VM {}", snapshot_name, name);
        return true;
    }
    
    bool revertToSnapshot(const std::string& snapshot_name) {
        std::lock_guard lock(status_mutex);
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain, snapshot_name.c_str(), 0);
        if (!snapshot) {
            std::println("❌ Snapshot '{}' not found for VM {}", snapshot_name, name);
            return false;
        }
        
        if (virDomainRevertToSnapshot(snapshot, 0) != 0) {
            virDomainSnapshotFree(snapshot);
            std::println("❌ Failed to revert to snapshot for VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        virDomainSnapshotFree(snapshot);
        
        std::println("↩️ Reverted to snapshot '{}' for VM {}", snapshot_name, name);
        return true;
    }
    
    bool scaleCPU(uint16_t vcpus) {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running && status != VMStatus::Paused) {
            std::println("❌ Cannot scale CPU for VM {}: not running or paused", name);
            return false;
        }
        
        // التحقق من الحدود
        for (auto& limit : resource_limits) {
            if (limit.type == ResourceType::CPU && 
                (vcpus < limit.min_value || vcpus > limit.max_value)) {
                std::println("❌ CPU value {} out of range [{}, {}] for VM {}", 
                            vcpus, limit.min_value, limit.max_value, name);
                return false;
            }
        }
        
        // تحديث التكوين
        config.vcpus = vcpus;
        
        // تطبيق التغيير على النطاق
        if (virDomainSetVcpus(domain, vcpus) != 0) {
            std::println("❌ Failed to scale CPU for VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        // تحديث حدود الموارد
        for (auto& limit : resource_limits) {
            if (limit.type == ResourceType::CPU) {
                limit.current_value = vcpus;
                break;
            }
        }
        
        applyResourceLimits();
        
        std::println("✅ CPU scaled to {} vCPUs for VM {}", vcpus, name);
        return true;
    }
    
    bool scaleMemory(uint64_t memory_mb) {
        std::lock_guard lock(status_mutex);
        
        if (status != VMStatus::Running && status != VMStatus::Paused) {
            std::println("❌ Cannot scale memory for VM {}: not running or paused", name);
            return false;
        }
        
        // التحقق من الحدود
        uint64_t memory_bytes = memory_mb * 1024 * 1024;
        for (auto& limit : resource_limits) {
            if (limit.type == ResourceType::Memory && 
                (memory_bytes < limit.min_value || memory_bytes > limit.max_value)) {
                std::println("❌ Memory value {}MB out of range [{}, {}] bytes for VM {}", 
                            memory_mb, limit.min_value, limit.max_value, name);
                return false;
            }
        }
        
        // تحديث التكوين
        config.memory_mb = memory_mb;
        
        // تطبيق التغيير على النطاق
        if (virDomainSetMemory(domain, memory_mb * 1024) != 0) {
            std::println("❌ Failed to scale memory for VM {}: {}", name, virGetLastErrorMessage());
            return false;
        }
        
        // تحديث حدود الموارد
        for (auto& limit : resource_limits) {
            if (limit.type == ResourceType::Memory) {
                limit.current_value = memory_bytes;
                break;
            }
        }
        
        applyResourceLimits();
        
        std::println("✅ Memory scaled to {} MB for VM {}", memory_mb, name);
        return true;
    }
    
    VMStatus getStatus() const {
        std::lock_guard lock(status_mutex);
        return status;
    }
    
    std::string getName() const {
        return name;
    }
    
    std::string getUUID() const {
        return uuid;
    }
    
    VMConfig getConfig() const {
        return config;
    }
    
    std::vector<ResourceLimit> getResourceLimits() const {
        return resource_limits;
    }
    
    virDomainPtr getDomain() const {
        return domain;
    }
    
    std::vector<VMSnapshot> getSnapshots() const {
        return snapshots;
    }
    
    std::string getStatusString() const {
        switch (status) {
            case VMStatus::Stopped: return "Stopped";
            case VMStatus::Running: return "Running";
            case VMStatus::Paused: return "Paused";
            case VMStatus::Error: return "Error";
            case VMStatus::Creating: return "Creating";
            case VMStatus::Migrating: return "Migrating";
            case VMStatus::Suspended: return "Suspended";
            default: return "Unknown";
        }
    }
    
private:
    std::string generateDomainXML() const {
        // إنشاء تكوين XML للنطاق الافتراضي
        return std::format(R"(
            <domain type='kvm'>
                <name>{}</name>
                <memory unit='MB'>{}</memory>
                <currentMemory unit='MB'>{}</currentMemory>
                <vcpu placement='static'>{}</vcpu>
                <os>
                    <type arch='{}'>{}</type>
                    <boot dev='hd'/>
                </os>
                <features>
                    <acpi/>
                    <apic/>
                    <vmport state='off'/>
                </features>
                <cpu mode='host-passthrough' check='none'/>
                <clock offset='utc'/>
                <on_poweroff>destroy</on_poweroff>
                <on_reboot>restart</on_reboot>
                <on_crash>destroy</on_crash>
                <devices>
                    <emulator>/usr/bin/qemu-system-{}</emulator>
                    <disk type='file' device='disk'>
                        <driver name='qemu' type='qcow2' cache='none' io='native'/>
                        <source file='{}'/>
                        <target dev='vda' bus='virtio'/>
                        <address type='pci' domain='0x0000' bus='0x00' slot='0x04' function='0x0'/>
                    </disk>
                    <controller type='usb' index='0' model='qemu-xhci' ports='15'/>
                    <controller type='pci' index='0' model='pcie-root'/>
                    <controller type='virtio-serial' index='0'/>
                    <interface type='bridge'>
                        <mac address='{}'/>
                        <source bridge='{}'/>
                        <model type='virtio'/>
                        <address type='pci' domain='0x0000' bus='0x00' slot='0x03' function='0x0'/>
                    </interface>
                    <serial type='pty'>
                        <target type='isa-serial' port='0'/>
                    </serial>
                    <console type='pty'/>
                    <channel type='unix'>
                        <target type='virtio' name='org.qemu.guest_agent.0'/>
                    </channel>
                    <input type='tablet' bus='usb'/>
                    <input type='mouse' bus='ps2'/>
                    <input type='keyboard' bus='ps2'/>
                    <graphics type='vnc' port='-1' listen='0.0.0.0'/>
                    <video>
                        <model type='{}' vram='{}' heads='1'/>
                        <address type='pci' domain='0x0000' bus='0x00' slot='0x02' function='0x0'/>
                    </video>
                    <memballoon model='virtio'>
                        <address type='pci' domain='0x0000' bus='0x00' slot='0x05' function='0x0'/>
                    </memballoon>
                </devices>
            </domain>
        )", 
        name, 
        config.memory_mb, 
        config.memory_mb,
        config.vcpus,
        config.arch, 
        config.os_type,
        config.arch,
        config.image_path,
        generateMACAddress(),
        config.network_bridge,
        config.video_model,
        config.video_vram);
    }
    
    std::string generateMACAddress() const {
        // إنشاء عنوان MAC عشوائي
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        return std::format("52:54:00:{:02x}:{:02x}:{:02x}", 
                          dis(gen), dis(gen), dis(gen));
    }
    
    void applyResourceLimits() {
        // تطبيق حدود الموارد عبر CGroup
        for (const auto& limit : resource_limits) {
            try {
                switch (limit.type) {
                    case ResourceType::CPU:
                        // تحويل النوى إلى وقت CPU (مثال: 1 نواة = 100000 ميكروثانية لكل 100000 ميكروثانية فترة)
                        cgroup_manager->setCPULimit(limit.current_value * 100000, 100000);
                        cgroup_manager->setCPUShares(1024); // قيمة افتراضية
                        break;
                    case ResourceType::Memory:
                        cgroup_manager->setMemoryLimit(limit.current_value);
                        cgroup_manager->setMemorySwappiness(10); // تقليل التبديل
                        break;
                    case ResourceType::IO:
                        // استخدام قيم افتراضية لـ IO
                        cgroup_manager->setIOLimit("sda", limit.current_value, limit.current_value);
                        break;
                    default:
                        break;
                }
            } catch (const std::exception& e) {
                std::println("❌ Failed to apply resource limit for VM {}: {}", name, e.what());
            }
        }
        
        // إضافة عملية VM إلى CGroup إذا كانت تعمل
        if (status == VMStatus::Running && domain) {
            virDomainInfo info;
            if (virDomainGetInfo(domain, &info) == 0 && info.state == VIR_DOMAIN_RUNNING) {
                // الحصول على معرفات عملية VM وإضافتها إلى CGroup
                // هذا الجزء معقد ويتطلب تفاعلاً مع libvirt بشكل أعمق
            }
        }
    }
};

// ===== مدير الآلات الافتراضية =====
class VirtualMachineManager {
private:
    std::shared_ptr<LibvirtConnection> libvirt_conn;
    std::unordered_map<std::string, std::unique_ptr<VirtualMachine>> virtual_machines;
    std::mutex vm_map_mutex;
    std::jthread maintenance_thread;
    std::atomic<bool> maintenance_active{false};
    std::vector<std::function<void(const std::string&, VMStatus)>> status_callbacks;
    
public:
    explicit VirtualMachineManager(std::shared_ptr<LibvirtConnection> conn)
        : libvirt_conn(conn) {
        
        maintenance_active = true;
        maintenance_thread = std::jthread([this](std::stop_token st) {
            maintenance_loop(st);
        });
        
        loadExistingVMs();
        
        std::println("👨‍💼 Virtual Machine Manager initialized");
    }
    
    ~VirtualMachineManager() {
        maintenance_active = false;
        if (maintenance_thread.joinable()) {
            maintenance_thread.request_stop();
            maintenance_thread.join();
        }
        
        // إيقاف جميع الآلات الافتراضية
        std::lock_guard lock(vm_map_mutex);
        for (auto& [name, vm] : virtual_machines) {
            vm->stop();
        }
        virtual_machines.clear();
        
        std::println("👋 Virtual Machine Manager shut down");
    }
    
    void registerStatusCallback(std::function<void(const std::string&, VMStatus)> callback) {
        status_callbacks.push_back(callback);
    }
    
    void notifyStatusChange(const std::string& vm_name, VMStatus status) {
        for (const auto& callback : status_callbacks) {
            callback(vm_name, status);
        }
    }
    
    bool createVM(const VMConfig& config) {
        std::lock_guard lock(vm_map_mutex);
        
        if (virtual_machines.find(config.name) != virtual_machines.end()) {
            std::println("❌ VM with name {} already exists", config.name);
            return false;
        }
        
        try {
            auto vm = std::make_unique<VirtualMachine>(libvirt_conn, config);
            if (vm->create()) {
                virtual_machines[config.name] = std::move(vm);
                std::println("✅ VM {} created successfully", config.name);
                notifyStatusChange(config.name, VMStatus::Stopped);
                return true;
            }
        } catch (const std::exception& e) {
            std::println("❌ Failed to create VM {}: {}", config.name, e.what());
        }
        
        return false;
    }
    
    bool startVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->start();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Running);
        }
        return result;
    }
    
    bool stopVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->stop();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Stopped);
        }
        return result;
    }
    
    bool shutdownVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->shutdown();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Stopped);
        }
        return result;
    }
    
    bool pauseVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->pause();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Paused);
        }
        return result;
    }
    
    bool resumeVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->resume();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Running);
        }
        return result;
    }
    
    bool restartVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        bool result = it->second->restart();
        if (result) {
            notifyStatusChange(vm_name, VMStatus::Running);
        }
        return result;
    }
    
    bool migrateVM(const std::string& vm_name, const std::string& destination_uri) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        return it->second->migrate(destination_uri);
    }
    
    bool createSnapshot(const std::string& vm_name, const std::string& snapshot_name, 
                       const std::string& description = "") {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        return it->second->createSnapshot(snapshot_name, description);
    }
    
    bool revertToSnapshot(const std::string& vm_name, const std::string& snapshot_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        return it->second->revertToSnapshot(snapshot_name);
    }
    
    bool scaleVMCPU(const std::string& vm_name, uint16_t vcpus) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        return it->second->scaleCPU(vcpus);
    }
    
    bool scaleVMMemory(const std::string& vm_name, uint64_t memory_mb) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            std::println("❌ VM {} not found", vm_name);
            return false;
        }
        
        return it->second->scaleMemory(memory_mb);
    }
    
    VMStatus getVMStatus(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            return VMStatus::Error;
        }
        
        return it->second->getStatus();
    }
    
    std::vector<std::string> listVMs() {
        std::lock_guard lock(vm_map_mutex);
        
        std::vector<std::string> vm_list;
        for (const auto& [name, vm] : virtual_machines) {
            vm_list.push_back(name);
        }
        
        return vm_list;
    }
    
    std::unique_ptr<VirtualMachine> removeVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            return nullptr;
        }
        
        // إيقاف الآلة الافتراضية أولاً
        it->second->stop();
        
        // إزالة الآلة من الخريطة وإرجاعها
        auto vm = std::move(it->second);
        virtual_machines.erase(it);
        
        std::println("🗑️ VM {} removed", vm_name);
        notifyStatusChange(vm_name, VMStatus::Stopped);
        return vm;
    }
    
    VirtualMachine* getVM(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            return nullptr;
        }
        
        return it->second.get();
    }
    
    std::vector<VMSnapshot> getVMSnapshots(const std::string& vm_name) {
        std::lock_guard lock(vm_map_mutex);
        
        auto it = virtual_machines.find(vm_name);
        if (it == virtual_machines.end()) {
            return {};
        }
        
        return it->second->getSnapshots();
    }
    
private:
    void loadExistingVMs() {
        // تحميل الآلات الافتراضية الموجودة من libvirt
        virDomainPtr* domains = nullptr;
        int num_domains = virConnectListAllDomains(libvirt_conn->get(), &domains, 
                                                  VIR_CONNECT_LIST_DOMAINS_ACTIVE | 
                                                  VIR_CONNECT_LIST_DOMAINS_INACTIVE);
        
        if (num_domains < 0) {
            std::println("❌ Failed to get domain list from libvirt");
            return;
        }
        
        for (int i = 0; i < num_domains; ++i) {
            virDomainPtr domain = domains[i];
            const char* name = virDomainGetName(domain);
            
            if (!name) continue;
            
            std::string vm_name(name);
            
            // تجنب تحميل الآلات التي تم تحميلها بالفعل
            if (virtual_machines.find(vm_name) != virtual_machines.end()) {
                virDomainFree(domain);
                continue;
            }
            
            // إنشاء تكوين للآلة الافتراضية الموجودة
            VMConfig config;
            config.name = vm_name;
            
            // الحصول على معلومات التكوين من النطاق
            virDomainInfo info;
            if (virDomainGetInfo(domain, &info) == 0) {
                config.vcpus = info.nrVirtCpu;
                config.memory_mb = info.memory / 1024;
            }
            
            // إضافة الآلة الافتراضية إلى المدير
            auto vm = std::make_unique<VirtualMachine>(libvirt_conn, config);
            vm->getDomain() = domain;
            
            // تحديد حالة الآلة
            VMStatus status = VMStatus::Stopped;
            if (info.state == VIR_DOMAIN_RUNNING) {
                status = VMStatus::Running;
            } else if (info.state == VIR_DOMAIN_PAUSED) {
                status = VMStatus::Paused;
            }
            
            virtual_machines[vm_name] = std::move(vm);
            notifyStatusChange(vm_name, status);
            
            std::println("📥 Loaded existing VM: {} ({})", vm_name, 
                        status == VMStatus::Running ? "Running" : "Stopped");
        }
        
        free(domains);
    }
    
    void maintenance_loop(std::stop_token st) {
        while (!st.stop_requested() && maintenance_active) {
            try {
                // فحص حالة جميع الآلات الافتراضية بانتظام
                check_vms_health();
                cleanup_old_snapshots();
                check_host_resources();
                std::this_thread::sleep_for(5s);
            } catch (const std::exception& e) {
                std::println("❌ Maintenance error: {}", e.what());
                std::this_thread::sleep_for(10s);
            }
        }
    }
    
    void check_vms_health() {
        std::lock_guard lock(vm_map_mutex);
        
        for (auto& [name, vm] : virtual_machines) {
            VMStatus status = vm->getStatus();
            
            // إذا كانت الآلة في حالة خطأ، حاول إعادة تشغيلها
            if (status == VMStatus::Error) {
                std::println("⚠️ VM {} is in error state, attempting recovery...", name);
                vm->stop();
                std::this_thread::sleep_for(2s);
                vm->start();
            }
            
            // إذا كانت الآلة مهاجرة لفترة طويلة، تحقق من حالتها
            if (status == VMStatus::Migrating) {
                // في التطبيق الحقيقي، نتحقق من حالة الهجرة هنا
                std::this_thread::sleep_for(1s);
            }
        }
    }
    
    void cleanup_old_snapshots() {
        std::lock_guard lock(vm_map_mutex);
        
        // تنظيف الـ snapshots القديمة (أقدم من 30 يوم)
        auto now = chrono::system_clock::now();
        auto threshold = now - 30 * 24h;
        
        for (auto& [name, vm] : virtual_machines) {
            auto snapshots = vm->getSnapshots();
            for (const auto& snapshot : snapshots) {
                if (snapshot.created_at < threshold) {
                    // حذف الـ snapshot القديمة
                    // هذا يتطلب تنفيذ دالة حذف الـ snapshots في VirtualMachine
                }
            }
        }
    }
    
    void check_host_resources() {
        // التحقق من موارد المضيف واتخاذ إجراء إذا كانت منخفضة
        // يمكن إضافة تنبيهات أو إجراءات تلقائية هنا
    }
};

// ===== نظام المراقبة في الوقت الحقيقي =====
class RealTimeMonitor {
private:
    std::shared_ptr<LibvirtConnection> libvirt_conn;
    std::shared_ptr<VirtualMachineManager> vm_manager;
    std::unordered_map<std::string, VMMetrics> vm_metrics;
    HostMetrics host_metrics;
    std::atomic<bool> monitoring_active{false};
    std::jthread monitoring_thread;
    std::mutex metrics_mutex;
    std::vector<std::function<void(const VMMetrics&)>> metrics_callbacks;
    std::vector<std::function<void(const HostMetrics&)>> host_metrics_callbacks;
    std::unordered_map<std::string, std::vector<ResourceUsage>> vm_metrics_history;
    
public:
    RealTimeMonitor(std::shared_ptr<LibvirtConnection> conn, 
                   std::shared_ptr<VirtualMachineManager> manager) 
        : libvirt_conn(conn), vm_manager(manager) {
        
        vm_metrics.reserve(20);
        metrics_callbacks.reserve(5);
        host_metrics_callbacks.reserve(3);
        vm_metrics_history.reserve(20);
        
        std::println("📊 RealTimeMonitor initialized");
    }
    
    ~RealTimeMonitor() {
        stop();
    }
    
    void start() {
        if (monitoring_active) return;
        
        monitoring_active = true;
        monitoring_thread = std::jthread([this](std::stop_token st) {
            monitoring_loop(st);
        });
        
        std::println("🔍 Real-time monitoring started");
    }
    
    void stop() {
        monitoring_active = false;
        if (monitoring_thread.joinable()) {
            monitoring_thread.request_stop();
            monitoring_thread.join();
        }
        std::println("⏹️ Real-time monitoring stopped");
    }
    
    void register_metrics_callback(std::function<void(const VMMetrics&)> callback) {
        std::lock_guard lock(metrics_mutex);
        metrics_callbacks.push_back(callback);
    }
    
    void register_host_metrics_callback(std::function<void(const HostMetrics&)> callback) {
        std::lock_guard lock(metrics_mutex);
        host_metrics_callbacks.push_back(callback);
    }
    
    VMMetrics get_vm_metrics(const std::string& vm_name) {
        std::lock_guard lock(metrics_mutex);
        auto it = vm_metrics.find(vm_name);
        return it != vm_metrics.end() ? it->second : VMMetrics{};
    }
    
    HostMetrics get_host_metrics() {
        std::lock_guard lock(metrics_mutex);
        return host_metrics;
    }
    
    std::unordered_map<std::string, VMMetrics> get_all_vm_metrics() {
        std::lock_guard lock(metrics_mutex);
        return vm_metrics;
    }
    
    std::vector<ResourceUsage> get_vm_metrics_history(const std::string& vm_name, 
                                                     size_t max_points = 100) {
        std::lock_guard lock(metrics_mutex);
        auto it = vm_metrics_history.find(vm_name);
        if (it == vm_metrics_history.end()) {
            return {};
        }
        
        if (it->second.size() <= max_points) {
            return it->second;
        }
        
        // إرجاع آخر النقاط المطلوبة
        return std::vector<ResourceUsage>(it->second.end() - max_points, it->second.end());
    }
    
    void clear_vm_metrics_history(const std::string& vm_name) {
        std::lock_guard lock(metrics_mutex);
        vm_metrics_history.erase(vm_name);
    }
    
private:
    void monitoring_loop(std::stop_token st) {
        while (!st.stop_requested() && monitoring_active) {
            try {
                update_vm_metrics();
                update_host_metrics();
                notify_subscribers();
                std::this_thread::sleep_for(1s);
            } catch (const std::exception& e) {
                std::println("❌ Monitoring error: {}", e.what());
                std::this_thread::sleep_for(5s);
            }
        }
    }
    
    void update_vm_metrics() {
        std::lock_guard lock(metrics_mutex);
        
        // الحصول على قائمة الآلات الافتراضية من المدير
        auto vm_list = vm_manager->listVMs();
        
        for (const auto& vm_name : vm_list) {
            auto* vm = vm_manager->getVM(vm_name);
            if (!vm) continue;
            
            VMMetrics metrics;
            metrics.vm_name = vm_name;
            metrics.usage.timestamp = chrono::system_clock::now();
            
            // إذا كانت الآلة غير نشطة، تخطّ جمع المقاييس
            if (vm->getStatus() != VMStatus::Running) {
                continue;
            }
            
            virDomainPtr domain = vm->getDomain();
            if (!domain) continue;
            
            // جمع إحصائيات CPU
            virDomainInfo info;
            if (virDomainGetInfo(domain, &info) == 0) {
                metrics.usage.cpu_percent = info.cpuTime / 1000000000.0;
            }
            
            // جمع إحصائيات الذاكرة
            virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
            unsigned int nr_stats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
            
            if (nr_stats > 0) {
                for (unsigned int j = 0; j < nr_stats; ++j) {
                    if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
                        metrics.usage.memory_bytes = stats[j].val;
                    } else if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_USABLE) {
                        metrics.usage.memory_max_bytes = stats[j].val;
                    }
                }
            }
            
            // تحديث المتوسطات المتحركة
            update_moving_averages(metrics);
            
            // تخزين المقاييس
            vm_metrics[vm_name] = metrics;
            
            // تخزين التاريخ
            vm_metrics_history[vm_name].push_back(metrics.usage);
            if (vm_metrics_history[vm_name].size() > 1000) {
                vm_metrics_history[vm_name].erase(vm_metrics_history[vm_name].begin());
            }
        }
    }
    
    void update_host_metrics() {
        std::lock_guard lock(metrics_mutex);
        
        // قراءة استخدام CPU من /proc/stat
        std::ifstream stat_file("/proc/stat");
        if (stat_file) {
            std::string line;
            std::getline(stat_file, line);
            std::istringstream iss(line);
            std::string cpu_label;
            uint64_t user, nice, system, idle;
            iss >> cpu_label >> user >> nice >> system >> idle;
            
            if (cpu_label == "cpu") {
                uint64_t total = user + nice + system + idle;
                host_metrics.cpu_load_1min = calculate_cpu_load(total, idle);
            }
        }
        
        // قراءة استخدام الذاكرة من /proc/meminfo
        std::ifstream meminfo_file("/proc/meminfo");
        if (meminfo_file) {
            std::string line;
            while (std::getline(meminfo_file, line)) {
                std::istringstream iss(line);
                std::string key;
                uint64_t value;
                std::string unit;
                iss >> key >> value >> unit;
                
                if (key == "MemTotal:") {
                    host_metrics.total_memory = value * 1024;
                } else if (key == "MemFree:") {
                    host_metrics.free_memory = value * 1024;
                } else if (key == "MemAvailable:") {
                    host_metrics.available_memory = value * 1024;
                }
            }
        }
        
        // قراءة استخدام القرص من /proc/diskstats
        host_metrics.disk_usage_percent = get_disk_usage();
    }
    
    uint64_t get_disk_usage() {
        // الحصول على استخدام القرص من نظام الملفات
        struct statvfs buf;
        if (statvfs("/", &buf) == 0) {
            uint64_t total = buf.f_blocks * buf.f_frsize;
            uint64_t available = buf.f_bavail * buf.f_frsize;
            return 100 - (available * 100 / total);
        }
        return 0;
    }
    
    template<typename T>
    auto calculate_moving_average(const std::vector<T>& data, size_t window) {
        if (data.empty()) return decltype(data[0] + data[0]){0};
        
        size_t elements = std::min(data.size(), window);
        auto sum = std::accumulate(data.end() - elements, data.end(), decltype(data[0] + data[0]){0});
        return sum / static_cast<decltype(sum)>(elements);
    }
    
    void update_moving_averages(VMMetrics& metrics) {
        metrics.cpu_history.push_back(metrics.usage.cpu_percent);
        if (metrics.cpu_history.size() > 300) {
            metrics.cpu_history.erase(metrics.cpu_history.begin());
        }
        
        metrics.memory_history.push_back(metrics.usage.memory_bytes);
        if (metrics.memory_history.size() > 300) {
            metrics.memory_history.erase(metrics.memory_history.begin());
        }
        
        metrics.cpu_avg_5min = calculate_moving_average(metrics.cpu_history, 60);
        metrics.cpu_avg_15min = calculate_moving_average(metrics.cpu_history, 180);
        metrics.memory_avg_5min = calculate_moving_average(metrics.memory_history, 60);
    }
    
    double calculate_cpu_load(uint64_t total, uint64_t idle) {
        static uint64_t prev_total = 0;
        static uint64_t prev_idle = 0;
        
        uint64_t total_diff = total - prev_total;
        uint64_t idle_diff = idle - prev_idle;
        
        prev_total = total;
        prev_idle = idle;
        
        if (total_diff == 0) return 0.0;
        
        return 100.0 * (total_diff - idle_diff) / total_diff;
    }
    
    void notify_subscribers() {
        std::lock_guard lock(metrics_mutex);
        
        for (const auto& [vm_name, metrics] : vm_metrics) {
            for (const auto& callback : metrics_callbacks) {
                callback(metrics);
            }
        }
        
        for (const auto& callback : host_metrics_callbacks) {
            callback(host_metrics);
        }
    }
};

// ===== محرك التوازن التلقائي الذكي =====
class AutoScalingEngine {
private:
    std::shared_ptr<RealTimeMonitor> monitor;
    std::shared_ptr<VirtualMachineManager> vm_manager;
    std::jthread decision_thread;
    std::atomic<bool> scaling_active{false};
    std::mutex decision_mutex;
    std::queue<ScalingDecision> decision_queue;
    std::vector<std::function<void(const ScalingDecision&)>> decision_callbacks;
    std::map<std::string, ResourceLimit> resource_limits;
    std::unordered_map<std::string, std::vector<ScalingDecision>> decision_history;
    
    // معاملات خوارزمية التوازن
    double cpu_scale_up_threshold{80.0};
    double cpu_scale_down_threshold{20.0};
    double mem_scale_up_threshold{85.0};
    double mem_scale_down_threshold{30.0};
    double io_scale_up_threshold{75.0};
    double io_scale_down_threshold{15.0};
    double network_scale_up_threshold{70.0};
    double network_scale_down_threshold{10.0};
    
    // معاملات خوارزمية التعلم
    std::unordered_map<std::string, std::vector<double>> vm_usage_patterns;
    std::unordered_map<std::string, chrono::system_clock::time_point> last_scale_time;
    std::unordered_map<std::string, int> scale_count_24h;
    
public:
    AutoScalingEngine(std::shared_ptr<RealTimeMonitor> mon,
                     std::shared_ptr<VirtualMachineManager> manager) 
        : monitor(mon), vm_manager(manager) {
        
        decision_callbacks.reserve(5);
        decision_history.reserve(20);
        vm_usage_patterns.reserve(20);
        last_scale_time.reserve(20);
        scale_count_24h.reserve(20);
        
        monitor->register_metrics_callback([this](const VMMetrics& metrics) {
            analyze_metrics(metrics);
        });
        
        std::println("⚖️ AutoScalingEngine initialized");
    }
    
    ~AutoScalingEngine() {
        stop();
    }
    
    void start() {
        if (scaling_active) return;
        
        scaling_active = true;
        decision_thread = std::jthread([this](std::stop_token st) {
            decision_loop(st);
        });
        
        std::println("🔧 Auto-scaling engine started");
    }
    
    void stop() {
        scaling_active = false;
        if (decision_thread.joinable()) {
            decision_thread.request_stop();
            decision_thread.join();
        }
        std::println("⏹️ Auto-scaling engine stopped");
    }
    
    void register_decision_callback(std::function<void(const ScalingDecision&)> callback) {
        std::lock_guard lock(decision_mutex);
        decision_callbacks.push_back(callback);
    }
    
    void set_resource_limits(const std::string& vm_name, const std::vector<ResourceLimit>& limits) {
        for (const auto& limit : limits) {
            std::string key = std::format("{}_{}", vm_name, static_cast<int>(limit.type));
            resource_limits[key] = limit;
        }
    }
    
    void set_scaling_thresholds(double cpu_up, double cpu_down, double mem_up, double mem_down,
                               double io_up = 75.0, double io_down = 15.0,
                               double net_up = 70.0, double net_down = 10.0) {
        cpu_scale_up_threshold = cpu_up;
        cpu_scale_down_threshold = cpu_down;
        mem_scale_up_threshold = mem_up;
        mem_scale_down_threshold = mem_down;
        io_scale_up_threshold = io_up;
        io_scale_down_threshold = io_down;
        network_scale_up_threshold = net_up;
        network_scale_down_threshold = net_down;
    }
    
    std::vector<ScalingDecision> get_decision_history(const std::string& vm_name) {
        std::lock_guard lock(decision_mutex);
        auto it = decision_history.find(vm_name);
        return it != decision_history.end() ? it->second : std::vector<ScalingDecision>{};
    }
    
    void apply_scaling_decision(const ScalingDecision& decision) {
        try {
            switch (decision.action) {
                case ScalingAction::ScaleUp:
                case ScalingAction::ScaleDown:
                    scale_resource(decision);
                    break;
                case ScalingAction::Migrate:
                    migrate_vm(decision);
                    break;
                case ScalingAction::Suspend:
                    suspend_vm(decision);
                    break;
                case ScalingAction::Resume:
                    resume_vm(decision);
                    break;
                default:
                    break;
            }
            
            // تحديث سجل القرارات
            update_decision_history(decision);
            
        } catch (const std::exception& e) {
            std::println("❌ Failed to apply scaling decision: {}", e.what());
        }
    }
    
    void train_usage_pattern(const std::string& vm_name, const std::vector<double>& usage_data) {
        // تدريب النموذج على أنماط استخدام VM
        vm_usage_patterns[vm_name] = usage_data;
    }
    
    double predict_usage(const std::string& vm_name) {
        // التنبؤ باستخدام VM بناءً على الأنماط التاريخية
        auto it = vm_usage_patterns.find(vm_name);
        if (it == vm_usage_patterns.end() || it->second.empty()) {
            return 0.0;
        }
        
        // خوارزمية تنبؤ بسيطة (يمكن استبدالها بنموذج ML)
        return std::accumulate(it->second.begin(), it->second.end(), 0.0) / it->second.size();
    }
    
private:
    void decision_loop(std::stop_token st) {
        while (!st.stop_requested() && scaling_active) {
            try {
                process_decisions();
                cleanup_old_decisions();
                std::this_thread::sleep_for(2s);
            } catch (const std::exception& e) {
                std::println("❌ Decision processing error: {}", e.what());
                std::this_thread::sleep_for(5s);
            }
        }
    }
    
    void analyze_metrics(const VMMetrics& metrics) {
        ScalingDecision decision;
        decision.vm_name = metrics.vm_name;
        decision.timestamp = chrono::system_clock::now();
        decision.action = ScalingAction::Maintain;
        
        // تحليل استخدام الموارد
        analyze_cpu_usage(metrics, decision);
        analyze_memory_usage(metrics, decision);
        analyze_io_usage(metrics, decision);
        analyze_network_usage(metrics, decision);
        
        // تحليل أنماط الاستخدام للتنبؤ
        analyze_usage_patterns(metrics, decision);
        
        // التحقق من حدود المعدل
        if (!check_rate_limit(metrics.vm_name, decision)) {
            decision.action = ScalingAction::Maintain;
        }
        
        if (decision.action != ScalingAction::Maintain) {
            std::lock_guard lock(decision_mutex);
            decision_queue.push(decision);
        }
    }
    
    void analyze_cpu_usage(const VMMetrics& metrics, ScalingDecision& decision) {
        double current_cpu = metrics.usage.cpu_percent;
        double avg_5min = metrics.cpu_avg_5min;
        
        std::string key = std::format("{}_{}", metrics.vm_name, static_cast<int>(ResourceType::CPU));
        if (!resource_limits.contains(key)) return;
        
        const auto& limit = resource_limits[key];
        
        if (current_cpu > cpu_scale_up_threshold && avg_5min > cpu_scale_up_threshold - 10) {
            decision.action = ScalingAction::ScaleUp;
            decision.resource = ResourceType::CPU;
            decision.amount = calculate_cpu_increase(limit, current_cpu);
            decision.confidence = calculate_confidence(current_cpu, avg_5min);
            decision.reason = std::format("High CPU usage: {:.2f}% (5min avg: {:.2f}%)", 
                                         current_cpu, avg_5min);
        }
        else if (current_cpu < cpu_scale_down_threshold && avg_5min < cpu_scale_down_threshold + 5) {
            decision.action = ScalingAction::ScaleDown;
            decision.resource = ResourceType::CPU;
            decision.amount = calculate_cpu_decrease(limit, current_cpu);
            decision.confidence = calculate_confidence(current_cpu, avg_5min);
            decision.reason = std::format("Low CPU usage: {:.2f}% (5min avg: {:.2f}%)", 
                                         current_cpu, avg_5min);
        }
    }
    
    void analyze_memory_usage(const VMMetrics& metrics, ScalingDecision& decision) {
        double memory_usage_percent = 100.0 * metrics.usage.memory_bytes / metrics.usage.memory_max_bytes;
        double avg_5min = 100.0 * metrics.memory_avg_5min / metrics.usage.memory_max_bytes;
        
        std::string key = std::format("{}_{}", metrics.vm_name, static_cast<int>(ResourceType::Memory));
        if (!resource_limits.contains(key)) return;
        
        const auto& limit = resource_limits[key];
        
        bool memory_more_critical = memory_usage_percent > mem_scale_up_threshold && 
                                   (decision.action == ScalingAction::Maintain || 
                                    memory_usage_percent > cpu_scale_up_threshold + 10);
        
        if (memory_more_critical && avg_5min > mem_scale_up_threshold - 10) {
            decision.action = ScalingAction::ScaleUp;
            decision.resource = ResourceType::Memory;
            decision.amount = calculate_memory_increase(limit, memory_usage_percent);
            decision.confidence = calculate_confidence(memory_usage_percent, avg_5min);
            decision.reason = std::format("High memory usage: {:.2f}% (5min avg: {:.2f}%)", 
                                         memory_usage_percent, avg_5min);
        }
        else if (memory_usage_percent < mem_scale_down_threshold && 
                avg_5min < mem_scale_down_threshold + 5 &&
                decision.action == ScalingAction::Maintain) {
            decision.action = ScalingAction::ScaleDown;
            decision.resource = ResourceType::Memory;
            decision.amount = calculate_memory_decrease(limit, memory_usage_percent);
            decision.confidence = calculate_confidence(memory_usage_percent, avg_5min);
            decision.reason = std::format("Low memory usage: {:.2f}% (5min avg: {:.2f}%)", 
                                         memory_usage_percent, avg_5min);
        }
    }
    
    void analyze_io_usage(const VMMetrics& metrics, ScalingDecision& decision) {
        // تحليل استخدام I/O (تنفيذ مبسط)
        // في التطبيق الحقيقي، نحتاج إلى جمع مقاييس I/O من libvirt أو نظام الملفات
    }
    
    void analyze_network_usage(const VMMetrics& metrics, ScalingDecision& decision) {
        // تحليل استخدام الشبكة (تنفيذ مبسط)
        // في التطبيق الحقيقي، نحتاج إلى جمع مقاييس الشبكة من libvirt أو النظام
    }
    
    void analyze_usage_patterns(const VMMetrics& metrics, ScalingDecision& decision) {
        // تحليل أنماط الاستخدام للتنبؤ بالحمل المستقبلي
        double predicted_usage = predict_usage(metrics.vm_name);
        
        if (predicted_usage > cpu_scale_up_threshold && decision.action == ScalingAction::Maintain) {
            decision.action = ScalingAction::ScaleUp;
            decision.resource = ResourceType::CPU;
            decision.amount = calculate_predicted_increase(metrics.vm_name, ResourceType::CPU, predicted_usage);
            decision.confidence = 0.6; // ثقة متوسطة في التنبؤ
            decision.reason = std::format("Predicted high usage: {:.2f}%", predicted_usage);
        }
    }
    
    uint64_t calculate_cpu_increase(const ResourceLimit& limit, double current_usage) {
        uint64_t current = limit.current_value;
        uint64_t increase = std::max<uint64_t>(1, static_cast<uint64_t>(current * 0.25));
        return std::min(current + increase, limit.max_value);
    }
    
    uint64_t calculate_cpu_decrease(const ResourceLimit& limit, double current_usage) {
        uint64_t current = limit.current_value;
        uint64_t decrease = std::max<uint64_t>(1, static_cast<uint64_t>(current * 0.25));
        return std::max(current - decrease, limit.min_value);
    }
    
    uint64_t calculate_memory_increase(const ResourceLimit& limit, double current_usage) {
        uint64_t current = limit.current_value;
        uint64_t increase = std::max<uint64_t>(1024 * 1024 * 1024, static_cast<uint64_t>(current * 0.25));
        return std::min(current + increase, limit.max_value);
    }
    
    uint64_t calculate_memory_decrease(const ResourceLimit& limit, double current_usage) {
        uint64_t current = limit.current_value;
        uint64_t decrease = std::max<uint64_t>(1024 * 1024 * 1024, static_cast<uint64_t>(current * 0.25));
        return std::max(current - decrease, limit.min_value);
    }
    
    uint64_t calculate_predicted_increase(const std::string& vm_name, ResourceType type, double predicted_usage) {
        std::string key = std::format("{}_{}", vm_name, static_cast<int>(type));
        if (!resource_limits.contains(key)) return 0;
        
        const auto& limit = resource_limits[key];
        uint64_t current = limit.current_value;
        
        // حساب الزيادة بناءً على الاستخدام المتوقع
        double scale_factor = predicted_usage / 100.0;
        uint64_t increase = std::max<uint64_t>(1, static_cast<uint64_t>(current * scale_factor * 0.3));
        
        return std::min(current + increase, limit.max_value);
    }
    
    double calculate_confidence(double current, double average) {
        double diff = std::abs(current - average);
        if (diff < 5) return 0.9;
        if (diff < 10) return 0.7;
        if (diff < 15) return 0.5;
        return 0.3;
    }
    
    bool check_rate_limit(const std::string& vm_name, const ScalingDecision& decision) {
        auto now = chrono::system_clock::now();
        
        // التحقق من وقت القرار الأخير
        auto last_scale = last_scale_time.find(vm_name);
        if (last_scale != last_scale_time.end()) {
            auto time_since_last = now - last_scale->second;
            if (time_since_last < 2min) {
                std::println("⚠️ Rate limit exceeded for VM {}, skipping decision", vm_name);
                return false;
            }
        }
        
        // التحقق من عدد القرارات في آخر 24 ساعة
        auto scale_count = scale_count_24h.find(vm_name);
        if (scale_count != scale_count_24h.end() && scale_count->second >= 50) {
            std::println("⚠️ Daily scale limit exceeded for VM {}, skipping decision", vm_name);
            return false;
        }
        
        // تحديث السجلات
        last_scale_time[vm_name] = now;
        scale_count_24h[vm_name]++;
        
        return true;
    }
    
    void process_decisions() {
        std::lock_guard lock(decision_mutex);
        
        while (!decision_queue.empty()) {
            ScalingDecision decision = decision_queue.front();
            decision_queue.pop();
            
            for (const auto& callback : decision_callbacks) {
                callback(decision);
            }
            
            // تطبيق قرار التحجيم تلقائيًا
            apply_scaling_decision(decision);
            
            std::println("📋 Scaling decision: {} {} for VM {} with {:.2f}% confidence - {}",
                scaling_action_to_string(decision.action),
                resource_type_to_string(decision.resource),
                decision.vm_name,
                decision.confidence * 100,
                decision.reason
            );
        }
    }
    
    void cleanup_old_decisions() {
        auto now = chrono::system_clock::now();
        auto threshold = now - 24h;
        
        std::lock_guard lock(decision_mutex);
        for (auto& [vm_name, decisions] : decision_history) {
            decisions.erase(
                std::remove_if(decisions.begin(), decisions.end(),
                    [threshold](const ScalingDecision& d) {
                        return d.timestamp < threshold;
                    }),
                decisions.end()
            );
        }
        
        // تنظيف عدادات القرارات اليومية
        for (auto it = scale_count_24h.begin(); it != scale_count_24h.end(); ) {
            if (it->second == 0) {
                it = scale_count_24h.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    void update_decision_history(const ScalingDecision& decision) {
        decision_history[decision.vm_name].push_back(decision);
        
        // الحفاظ على تاريخ القرارات محدودًا
        if (decision_history[decision.vm_name].size() > 1000) {
            decision_history[decision.vm_name].erase(
                decision_history[decision.vm_name].begin(),
                decision_history[decision.vm_name].begin() + 100
            );
        }
    }
    
    void scale_resource(const ScalingDecision& decision) {
        if (decision.resource == ResourceType::CPU) {
            vm_manager->scaleVMCPU(decision.vm_name, decision.amount);
        } else if (decision.resource == ResourceType::Memory) {
            vm_manager->scaleVMMemory(decision.vm_name, decision.amount / (1024 * 1024));
        }
    }
    
    void migrate_vm(const ScalingDecision& decision) {
        vm_manager->migrateVM(decision.vm_name, "qemu+ssh://destination-host/system");
    }
    
    void suspend_vm(const ScalingDecision& decision) {
        vm_manager->pauseVM(decision.vm_name);
    }
    
    void resume_vm(const ScalingDecision& decision) {
        vm_manager->resumeVM(decision.vm_name);
    }
    
    std::string scaling_action_to_string(ScalingAction action) {
        switch (action) {
            case ScalingAction::ScaleUp: return "ScaleUp";
            case ScalingAction::ScaleDown: return "ScaleDown";
            case ScalingAction::Maintain: return "Maintain";
            case ScalingAction::Migrate: return "Migrate";
            case ScalingAction::Suspend: return "Suspend";
            case ScalingAction::Resume: return "Resume";
            default: return "Unknown";
        }
    }
    
    std::string resource_type_to_string(ResourceType type) {
        switch (type) {
            case ResourceType::CPU: return "CPU";
            case ResourceType::Memory: return "Memory";
            case ResourceType::IO: return "I/O";
            case ResourceType::Network: return "Network";
            default: return "Unknown";
        }
    }
};

// ===== منفذ تنفيذ القرارات =====
class DecisionExecutor {
private:
    std::shared_ptr<RealTimeMonitor> monitor;
    std::shared_ptr<VirtualMachineManager> vm_manager;
    std::shared_ptr<AutoScalingEngine> scaling_engine;
    std::jthread execution_thread;
    std::atomic<bool> execution_active{false};
    std::mutex execution_mutex;
    std::queue<ScalingDecision> execution_queue;
    std::condition_variable execution_cv;
    std::unordered_map<std::string, chrono::system_clock::time_point> last_execution_time;
    
public:
    DecisionExecutor(std::shared_ptr<RealTimeMonitor> mon,
                    std::shared_ptr<VirtualMachineManager> manager,
                    std::shared_ptr<AutoScalingEngine> engine)
        : monitor(mon), vm_manager(manager), scaling_engine(engine) {
        std::println("🚀 DecisionExecutor initialized");
    }
    
    ~DecisionExecutor() {
        stop();
    }
    
    void start() {
        if (execution_active) return;
        
        execution_active = true;
        execution_thread = std::jthread([this](std::stop_token st) {
            execution_loop(st);
        });
        
        std::println("🔧 Decision executor started");
    }
    
    void stop() {
        execution_active = false;
        execution_cv.notify_all();
        if (execution_thread.joinable()) {
            execution_thread.request_stop();
            execution_thread.join();
        }
        std::println("⏹️ Decision executor stopped");
    }
    
    void schedule_execution(const ScalingDecision& decision) {
        std::lock_guard lock(execution_mutex);
        
        // التحقق من معدل التنفيذ
        auto now = chrono::system_clock::now();
        auto last_exec = last_execution_time.find(decision.vm_name);
        
        if (last_exec != last_execution_time.end()) {
            auto time_since_last = now - last_exec->second;
            if (time_since_last < 30s) {
                std::println("⚠️ Execution rate limit exceeded for VM {}, skipping decision", 
                            decision.vm_name);
                return;
            }
        }
        
        execution_queue.push(decision);
        last_execution_time[decision.vm_name] = now;
        execution_cv.notify_one();
    }
    
    size_t get_queue_size() const {
        std::lock_guard lock(execution_mutex);
        return execution_queue.size();
    }
    
private:
    void execution_loop(std::stop_token st) {
        while (!st.stop_requested() && execution_active) {
            ScalingDecision decision;
            
            {
                std::unique_lock lock(execution_mutex);
                
                while (execution_queue.empty() && !st.stop_requested()) {
                    execution_cv.wait_for(lock, 100ms);
                }
                
                if (st.stop_requested()) break;
                if (execution_queue.empty()) continue;
                
                decision = execution_queue.front();
                execution_queue.pop();
            }
            
            execute_decision(decision);
        }
    }
    
    void execute_decision(const ScalingDecision& decision) {
        try {
            std::println("🔨 Executing decision: {} {} for VM {}",
                scaling_action_to_string(decision.action),
                resource_type_to_string(decision.resource),
                decision.vm_name
            );
            
            switch (decision.action) {
                case ScalingAction::ScaleUp:
                    scale_up_resource(decision);
                    break;
                case ScalingAction::ScaleDown:
                    scale_down_resource(decision);
                    break;
                case ScalingAction::Migrate:
                    migrate_vm(decision);
                    break;
                case ScalingAction::Suspend:
                    suspend_vm(decision);
                    break;
                case ScalingAction::Resume:
                    resume_vm(decision);
                    break;
                default:
                    break;
            }
            
            std::println("✅ Successfully executed decision for VM {}", decision.vm_name);
            
        } catch (const std::exception& e) {
            std::println("❌ Failed to execute decision for VM {}: {}", 
                        decision.vm_name, e.what());
            
            // إعادة جدولة القرار بعد تأخير
            std::this_thread::sleep_for(5s);
            schedule_execution(decision);
        }
    }
    
    void scale_up_resource(const ScalingDecision& decision) {
        scaling_engine->apply_scaling_decision(decision);
    }
    
    void scale_down_resource(const ScalingDecision& decision) {
        scaling_engine->apply_scaling_decision(decision);
    }
    
    void migrate_vm(const ScalingDecision& decision) {
        scaling_engine->apply_scaling_decision(decision);
    }
    
    void suspend_vm(const ScalingDecision& decision) {
        vm_manager->pauseVM(decision.vm_name);
    }
    
    void resume_vm(const ScalingDecision& decision) {
        vm_manager->resumeVM(decision.vm_name);
    }
    
    std::string scaling_action_to_string(ScalingAction action) {
        switch (action) {
            case ScalingAction::ScaleUp: return "ScaleUp";
            case ScalingAction::ScaleDown: return "ScaleDown";
            case ScalingAction::Maintain: return "Maintain";
            case ScalingAction::Migrate: return "Migrate";
            case ScalingAction::Suspend: return "Suspend";
            case ScalingAction::Resume: return "Resume";
            default: return "Unknown";
        }
    }
    
    std::string resource_type_to_string(ResourceType type) {
        switch (type) {
            case ResourceType::CPU: return "CPU";
            case ResourceType::Memory: return "Memory";
            case ResourceType::IO: return "I/O";
            case ResourceType::Network: return "Network";
            default: return "Unknown";
        }
    }
};

// ===== واجهة المستخدم =====
class UserInterface {
private:
    std::shared_ptr<VirtualMachineManager> vm_manager;
    std::shared_ptr<RealTimeMonitor> monitor;
    std::shared_ptr<AutoScalingEngine> scaling_engine;
    std::shared_ptr<DecisionExecutor> decision_executor;
    std::atomic<bool> ui_active{false};
    std::jthread ui_thread;
    std::mutex ui_mutex;
    
public:
    UserInterface(std::shared_ptr<VirtualMachineManager> manager,
                 std::shared_ptr<RealTimeMonitor> mon,
                 std::shared_ptr<AutoScalingEngine> engine,
                 std::shared_ptr<DecisionExecutor> executor)
        : vm_manager(manager), monitor(mon), scaling_engine(engine), decision_executor(executor) {
        std::println("🎨 User Interface initialized");
    }
    
    ~UserInterface() {
        stop();
    }
    
    void start() {
        if (ui_active) return;
        
        ui_active = true;
        ui_thread = std::jthread([this](std::stop_token st) {
            ui_loop(st);
        });
        
        std::println("🖥️ User Interface started");
    }
    
    void stop() {
        ui_active = false;
        if (ui_thread.joinable()) {
            ui_thread.request_stop();
            ui_thread.join();
        }
        std::println("⏹️ User Interface stopped");
    }
    
    void show_menu() {
        std::println("\n=== Virtual Machine Manager ===");
        std::println("1. List virtual machines");
        std::println("2. Create new virtual machine");
        std::println("3. Start virtual machine");
        std::println("4. Stop virtual machine");
        std::println("5. Shutdown virtual machine");
        std::println("6. Restart virtual machine");
        std::println("7. Pause virtual machine");
        std::println("8. Resume virtual machine");
        std::println("9. Show metrics");
        std::println("10. Show scaling decisions");
        std::println("11. Create snapshot");
        std::println("12. Revert to snapshot");
        std::println("13. Scale resources");
        std::println("14. Migrate VM");
        std::println("15. Exit");
        std::print("Choose an option: ");
    }
    
    void handle_input() {
        std::string input;
        std::getline(std::cin, input);
        
        try {
            int choice = std::stoi(input);
            
            switch (choice) {
                case 1:
                    list_virtual_machines();
                    break;
                case 2:
                    create_virtual_machine();
                    break;
                case 3:
                    start_virtual_machine();
                    break;
                case 4:
                    stop_virtual_machine();
                    break;
                case 5:
                    shutdown_virtual_machine();
                    break;
                case 6:
                    restart_virtual_machine();
                    break;
                case 7:
                    pause_virtual_machine();
                    break;
                case 8:
                    resume_virtual_machine();
                    break;
                case 9:
                    show_metrics();
                    break;
                case 10:
                    show_scaling_decisions();
                    break;
                case 11:
                    create_snapshot();
                    break;
                case 12:
                    revert_to_snapshot();
                    break;
                case 13:
                    scale_resources();
                    break;
                case 14:
                    migrate_vm();
                    break;
                case 15:
                    ui_active = false;
                    break;
                default:
                    std::println("Invalid option. Please try again.");
            }
        } catch (const std::exception& e) {
            std::println("Error: {}", e.what());
        }
    }
    
private:
    void ui_loop(std::stop_token st) {
        while (!st.stop_requested() && ui_active) {
            show_menu();
            handle_input();
            std::this_thread::sleep_for(500ms);
        }
    }
    
    void list_virtual_machines() {
        auto vm_list = vm_manager->listVMs();
        
        std::println("\n=== Virtual Machines ===");
        for (const auto& vm_name : vm_list) {
            VMStatus status = vm_manager->getVMStatus(vm_name);
            auto* vm = vm_manager->getVM(vm_name);
            
            if (vm) {
                std::println("{} - {} - {}", vm_name, vm->getStatusString(), vm->getUUID());
            } else {
                std::println("{} - {}", vm_name, vm_status_to_string(status));
            }
        }
    }
    
    void create_virtual_machine() {
        std::string name, image_path, network_bridge;
        uint16_t vcpus, video_vram;
        uint64_t memory_mb;
        
        std::print("Enter VM name: ");
        std::getline(std::cin, name);
        
        std::print("Enter image path: ");
        std::getline(std::cin, image_path);
        
        std::print("Enter vCPUs: ");
        std::string vcpus_str;
        std::getline(std::cin, vcpus_str);
        vcpus = std::stoi(vcpus_str);
        
        std::print("Enter memory (MB): ");
        std::string memory_str;
        std::getline(std::cin, memory_str);
        memory_mb = std::stoull(memory_str);
        
        std::print("Enter network bridge (default: virbr0): ");
        std::getline(std::cin, network_bridge);
        if (network_bridge.empty()) network_bridge = "virbr0";
        
        std::print("Enter video VRAM (default: 16384): ");
        std::string vram_str;
        std::getline(std::cin, vram_str);
        video_vram = vram_str.empty() ? 16384 : std::stoi(vram_str);
        
        VMConfig config;
        config.name = name;
        config.image_path = image_path;
        config.vcpus = vcpus;
        config.memory_mb = memory_mb;
        config.network_bridge = network_bridge;
        config.video_vram = video_vram;
        
        // إضافة حدود الموارد الافتراضية
        config.limits = {
            {ResourceType::CPU, 1, 32, vcpus, "cores"},
            {ResourceType::Memory, 512 * 1024 * 1024, 64 * 1024 * 1024 * 1024, 
             memory_mb * 1024 * 1024, "bytes"}
        };
        
        if (vm_manager->createVM(config)) {
            std::println("✅ VM {} created successfully", name);
            scaling_engine->set_resource_limits(name, config.limits);
        } else {
            std::println("❌ Failed to create VM {}", name);
        }
    }
    
    void start_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to start: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->startVM(vm_name)) {
            std::println("✅ VM {} started", vm_name);
        } else {
            std::println("❌ Failed to start VM {}", vm_name);
        }
    }
    
    void stop_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to stop: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->stopVM(vm_name)) {
            std::println("✅ VM {} stopped", vm_name);
        } else {
            std::println("❌ Failed to stop VM {}", vm_name);
        }
    }
    
    void shutdown_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to shutdown: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->shutdownVM(vm_name)) {
            std::println("✅ VM {} shutdown", vm_name);
        } else {
            std::println("❌ Failed to shutdown VM {}", vm_name);
        }
    }
    
    void restart_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to restart: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->restartVM(vm_name)) {
            std::println("✅ VM {} restarted", vm_name);
        } else {
            std::println("❌ Failed to restart VM {}", vm_name);
        }
    }
    
    void pause_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to pause: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->pauseVM(vm_name)) {
            std::println("✅ VM {} paused", vm_name);
        } else {
            std::println("❌ Failed to pause VM {}", vm_name);
        }
    }
    
    void resume_virtual_machine() {
        std::string vm_name;
        std::print("Enter VM name to resume: ");
        std::getline(std::cin, vm_name);
        
        if (vm_manager->resumeVM(vm_name)) {
            std::println("✅ VM {} resumed", vm_name);
        } else {
            std::println("❌ Failed to resume VM {}", vm_name);
        }
    }
    
    void show_metrics() {
        auto vm_list = vm_manager->listVMs();
        
        std::println("\n=== VM Metrics ===");
        for (const auto& vm_name : vm_list) {
            auto metrics = monitor->get_vm_metrics(vm_name);
            std::println("{}: CPU {:.2f}%, Memory {:.2f}%", 
                vm_name, 
                metrics.usage.cpu_percent,
                100.0 * metrics.usage.memory_bytes / metrics.usage.memory_max_bytes);
        }
        
        auto host_metrics = monitor->get_host_metrics();
        std::println("\n=== Host Metrics ===");
        std::println("CPU Load: {:.2f}%", host_metrics.cpu_load_1min);
        std::println("Memory: {:.2f}% used", 
            100.0 * (host_metrics.total_memory - host_metrics.available_memory) / host_metrics.total_memory);
        std::println("Disk: {}% used", host_metrics.disk_usage_percent);
    }
    
    void show_scaling_decisions() {
        auto vm_list = vm_manager->listVMs();
        
        std::println("\n=== Scaling Decisions ===");
        for (const auto& vm_name : vm_list) {
            auto decisions = scaling_engine->get_decision_history(vm_name);
            if (!decisions.empty()) {
                std::println("{}: {} decisions", vm_name, decisions.size());
                for (const auto& decision : decisions) {
                    auto time_str = std::format("{:%Y-%m-%d %H:%M:%S}", 
                                               chrono::floor<chrono::seconds>(decision.timestamp));
                    std::println("  - [{}] {} {} (confidence: {:.2f}%) - {}", 
                        time_str,
                        scaling_action_to_string(decision.action),
                        resource_type_to_string(decision.resource),
                        decision.confidence * 100,
                        decision.reason);
                }
            }
        }
        
        std::println("Pending decisions in queue: {}", decision_executor->get_queue_size());
    }
    
    void create_snapshot() {
        std::string vm_name, snapshot_name, description;
        
        std::print("Enter VM name: ");
        std::getline(std::cin, vm_name);
        
        std::print("Enter snapshot name: ");
        std::getline(std::cin, snapshot_name);
        
        std::print("Enter snapshot description (optional): ");
        std::getline(std::cin, description);
        
        if (vm_manager->createSnapshot(vm_name, snapshot_name, description)) {
            std::println("✅ Snapshot '{}' created for VM {}", snapshot_name, vm_name);
        } else {
            std::println("❌ Failed to create snapshot for VM {}", vm_name);
        }
    }
    
    void revert_to_snapshot() {
        std::string vm_name, snapshot_name;
        
        std::print("Enter VM name: ");
        std::getline(std::cin, vm_name);
        
        std::print("Enter snapshot name: ");
        std::getline(std::cin, snapshot_name);
        
        if (vm_manager->revertToSnapshot(vm_name, snapshot_name)) {
            std::println("✅ Reverted to snapshot '{}' for VM {}", snapshot_name, vm_name);
        } else {
            std::println("❌ Failed to revert to snapshot for VM {}", vm_name);
        }
    }
    
    void scale_resources() {
        std::string vm_name, resource_type_str;
        uint64_t value;
        
        std::print("Enter VM name: ");
        std::getline(std::cin, vm_name);
        
        std::print("Enter resource type (cpu/memory): ");
        std::getline(std::cin, resource_type_str);
        
        std::print("Enter new value: ");
        std::string value_str;
        std::getline(std::cin, value_str);
        value = std::stoull(value_str);
        
        if (resource_type_str == "cpu") {
            if (vm_manager->scaleVMCPU(vm_name, value)) {
                std::println("✅ CPU scaled to {} for VM {}", value, vm_name);
            } else {
                std::println("❌ Failed to scale CPU for VM {}", vm_name);
            }
        } else if (resource_type_str == "memory") {
            if (vm_manager->scaleVMMemory(vm_name, value)) {
                std::println("✅ Memory scaled to {}MB for VM {}", value, vm_name);
            } else {
                std::println("❌ Failed to scale memory for VM {}", vm_name);
            }
        } else {
            std::println("❌ Invalid resource type: {}", resource_type_str);
        }
    }
    
    void migrate_vm() {
        std::string vm_name, destination_uri;
        
        std::print("Enter VM name: ");
        std::getline(std::cin, vm_name);
        
        std::print("Enter destination URI (e.g., qemu+ssh://hostname/system): ");
        std::getline(std::cin, destination_uri);
        
        if (vm_manager->migrateVM(vm_name, destination_uri)) {
            std::println("✅ VM {} migrated to {}", vm_name, destination_uri);
        } else {
            std::println("❌ Failed to migrate VM {}", vm_name);
        }
    }
    
    std::string vm_status_to_string(VMStatus status) {
        switch (status) {
            case VMStatus::Stopped: return "Stopped";
            case VMStatus::Running: return "Running";
            case VMStatus::Paused: return "Paused";
            case VMStatus::Error: return "Error";
            case VMStatus::Creating: return "Creating";
            case VMStatus::Migrating: return "Migrating";
            case VMStatus::Suspended: return "Suspended";
            default: return "Unknown";
        }
    }
    
    std::string scaling_action_to_string(ScalingAction action) {
        switch (action) {
            case ScalingAction::ScaleUp: return "ScaleUp";
            case ScalingAction::ScaleDown: return "ScaleDown";
            case ScalingAction::Maintain: return "Maintain";
            case ScalingAction::Migrate: return "Migrate";
            case ScalingAction::Suspend: return "Suspend";
            case ScalingAction::Resume: return "Resume";
            default: return "Unknown";
        }
    }
    
    std::string resource_type_to_string(ResourceType type) {
        switch (type) {
            case ResourceType::CPU: return "CPU";
            case ResourceType::Memory: return "Memory";
            case ResourceType::IO: return "I/O";
            case ResourceType::Network: return "Network";
            default: return "Unknown";
        }
    }
};

// ===== النظام الرئيسي للتوسع الذكي =====
class IntelligentScalingSystem {
private:
    std::shared_ptr<LibvirtConnection> libvirt_conn;
    std::shared_ptr<VirtualMachineManager> vm_manager;
    std::shared_ptr<RealTimeMonitor> monitor;
    std::shared_ptr<AutoScalingEngine> scaling_engine;
    std::shared_ptr<DecisionExecutor> decision_executor;
    std::shared_ptr<UserInterface> user_interface;
    std::atomic<bool> system_active{false};
    std::string config_file_path{"./vm_manager_config.json"};
    
public:
    IntelligentScalingSystem() {
        std::println("🎮 Initializing IntelligentScalingSystem...");
        
        libvirt_conn = std::make_shared<LibvirtConnection>();
        vm_manager = std::make_shared<VirtualMachineManager>(libvirt_conn);
        monitor = std::make_shared<RealTimeMonitor>(libvirt_conn, vm_manager);
        scaling_engine = std::make_shared<AutoScalingEngine>(monitor, vm_manager);
        decision_executor = std::make_shared<DecisionExecutor>(monitor, vm_manager, scaling_engine);
        user_interface = std::make_shared<UserInterface>(vm_manager, monitor, scaling_engine, decision_executor);
        
        setup_subscriptions();
        load_configuration();
        
        std::println("✅ IntelligentScalingSystem fully initialized");
        std::println("Hypervisor: {} on host: {}", 
            libvirt_conn->getHypervisorVersion(), 
            libvirt_conn->getHostname());
    }
    
    ~IntelligentScalingSystem() {
        stop();
        save_configuration();
    }
    
    void start() {
        if (system_active) return;
        
        system_active = true;
        monitor->start();
        scaling_engine->start();
        decision_executor->start();
        user_interface->start();
        
        std::println("🚀 Intelligent scaling system started");
    }
    
    void stop() {
        if (!system_active) return;
        
        system_active = false;
        user_interface->stop();
        decision_executor->stop();
        scaling_engine->stop();
        monitor->stop();
        
        std::println("🛑 Intelligent scaling system stopped");
    }
    
    void configure_scaling(const std::string& vm_name, const std::vector<ResourceLimit>& limits) {
        scaling_engine->set_resource_limits(vm_name, limits);
    }
    
    void set_scaling_thresholds(double cpu_up, double cpu_down, double mem_up, double mem_down,
                               double io_up = 75.0, double io_down = 15.0,
                               double net_up = 70.0, double net_down = 10.0) {
        scaling_engine->set_scaling_thresholds(cpu_up, cpu_down, mem_up, mem_down, 
                                              io_up, io_down, net_up, net_down);
    }
    
    std::vector<ScalingDecision> get_decisions(const std::string& vm_name) {
        return scaling_engine->get_decision_history(vm_name);
    }
    
    bool add_virtual_machine(const VMConfig& config) {
        return vm_manager->createVM(config);
    }
    
    bool start_virtual_machine(const std::string& vm_name) {
        return vm_manager->startVM(vm_name);
    }
    
    bool stop_virtual_machine(const std::string& vm_name) {
        return vm_manager->stopVM(vm_name);
    }
    
    bool shutdown_virtual_machine(const std::string& vm_name) {
        return vm_manager->shutdownVM(vm_name);
    }
    
    bool restart_virtual_machine(const std::string& vm_name) {
        return vm_manager->restartVM(vm_name);
    }
    
    bool pause_virtual_machine(const std::string& vm_name) {
        return vm_manager->pauseVM(vm_name);
    }
    
    bool resume_virtual_machine(const std::string& vm_name) {
        return vm_manager->resumeVM(vm_name);
    }
    
    bool migrate_virtual_machine(const std::string& vm_name, const std::string& destination_uri) {
        return vm_manager->migrateVM(vm_name, destination_uri);
    }
    
    bool create_snapshot(const std::string& vm_name, const std::string& snapshot_name, 
                        const std::string& description = "") {
        return vm_manager->createSnapshot(vm_name, snapshot_name, description);
    }
    
    bool revert_to_snapshot(const std::string& vm_name, const std::string& snapshot_name) {
        return vm_manager->revertToSnapshot(vm_name, snapshot_name);
    }
    
    bool scale_vm_cpu(const std::string& vm_name, uint16_t vcpus) {
        return vm_manager->scaleVMCPU(vm_name, vcpus);
    }
    
    bool scale_vm_memory(const std::string& vm_name, uint64_t memory_mb) {
        return vm_manager->scaleVMMemory(vm_name, memory_mb);
    }
    
    std::vector<std::string> list_virtual_machines() {
        return vm_manager->listVMs();
    }
    
    VMStatus get_virtual_machine_status(const std::string& vm_name) {
        return vm_manager->getVMStatus(vm_name);
    }
    
    std::vector<VMSnapshot> get_virtual_machine_snapshots(const std::string& vm_name) {
        return vm_manager->getVMSnapshots(vm_name);
    }
    
    void set_config_file_path(const std::string& path) {
        config_file_path = path;
    }
    
private:
    void setup_subscriptions() {
        scaling_engine->register_decision_callback([this](const ScalingDecision& decision) {
            decision_executor->schedule_execution(decision);
        });
        
        vm_manager->registerStatusCallback([this](const std::string& vm_name, VMStatus status) {
            handle_vm_status_change(vm_name, status);
        });
    }
    
    void handle_vm_status_change(const std::string& vm_name, VMStatus status) {
        std::println("📢 VM {} status changed to {}", vm_name, vm_status_to_string(status));
        
        // يمكن إضافة إجراءات إضافية هنا عند تغيير حالة VM
    }
    
    void load_configuration() {
        // تحميل التكوين من ملف
        try {
            if (fs::exists(config_file_path)) {
                std::ifstream config_file(config_file_path);
                // معالجة ملف التكوين (يمكن استخدام JSON parser في التطبيق الحقيقي)
                std::println("📂 Loaded configuration from {}", config_file_path);
            }
        } catch (const std::exception& e) {
            std::println("❌ Failed to load configuration: {}", e.what());
        }
    }
    
    void save_configuration() {
        // حفظ التكوين إلى ملف
        try {
            std::ofstream config_file(config_file_path);
            // كتابة التكوين إلى الملف (يمكن استخدام JSON library في التطبيق الحقيقي)
            std::println("💾 Saved configuration to {}", config_file_path);
        } catch (const std::exception& e) {
            std::println("❌ Failed to save configuration: {}", e.what());
        }
    }
    
    std::string vm_status_to_string(VMStatus status) {
        switch (status) {
            case VMStatus::Stopped: return "Stopped";
            case VMStatus::Running: return "Running";
            case VMStatus::Paused: return "Paused";
            case VMStatus::Error: return "Error";
            case VMStatus::Creating: return "Creating";
            case VMStatus::Migrating: return "Migrating";
            case VMStatus::Suspended: return "Suspended";
            default: return "Unknown";
        }
    }
};

// ===== معالج الإشارات =====
class SignalHandler {
private:
    static std::atomic<bool> exit_requested;
    
public:
    static void setup() {
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGHUP, handle_signal);
    }
    
    static bool should_exit() {
        return exit_requested;
    }
    
private:
    static void handle_signal(int signal) {
        std::println("\n📶 Received signal {}, shutting down...", signal);
        exit_requested = true;
    }
};

std::atomic<bool> SignalHandler::exit_requested{false};

// ===== الدالة الرئيسية =====
int main() {
    try {
        std::println("🚀 Starting Virtual Resource Manager...");
        
        // إعداد معالج الإشارات
        SignalHandler::setup();
        
        // إنشاء النظام
        IntelligentScalingSystem system;
        
        // تكوين إعدادات التوازن
        system.set_scaling_thresholds(75.0, 25.0, 80.0, 35.0);
        
        // إنشاء تكوين لآلة افتراضية افتراضية
        VMConfig vm_config;
        vm_config.name = "ubuntu-vm";
        vm_config.image_path = "/var/lib/libvirt/images/ubuntu.qcow2";
        vm_config.vcpus = 2;
        vm_config.memory_mb = 2048;
        
        // إضافة حدود الموارد
        vm_config.limits = {
            {ResourceType::CPU, 1, 16, 2, "cores"},
            {ResourceType::Memory, 1024 * 1024 * 1024, 16 * 1024 * 1024 * 1024, 
             2 * 1024 * 1024 * 1024, "bytes"}
        };
        
        // إضافة الآلة الافتراضية
        if (system.add_virtual_machine(vm_config)) {
            system.configure_scaling("ubuntu-vm", vm_config.limits);
        }
        
        // بدء النظام
        system.start();
        
        std::println("✅ System started successfully!");
        std::println("Use the menu to manage virtual machines...");
        
        // الانتظار لإشارة الإيقاف
        while (!SignalHandler::should_exit()) {
            std::this_thread::sleep_for(1s);
        }
        
        // إيقاف النظام
        system.stop();
        
        std::println("👋 Virtual Resource Manager shut down successfully");
        return 0;
        
    } catch (const std::exception& e) {
        std::println("❌ Fatal error: {}", e.what());
        return 1;
    }
}