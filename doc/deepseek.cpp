#include <libvirt/libvirt.h>
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

namespace fs = std::filesystem;
namespace rng = std::ranges;
namespace chrono = std::chrono;

using namespace std::chrono_literals;

// ===== الأنواع والأحداث =====
enum class VMStatus { Stopped, Running, Paused, Error };
enum class ResourceType { CPU, Memory, IO, Network };
enum class ScalingAction { ScaleUp, ScaleDown, Maintain, Migrate };

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
};

struct VMConfig {
    std::string name;
    std::vector<ResourceLimit> limits;
    std::string image_path;
    uint16_t vcpus{2};
    uint64_t memory_mb{2048};
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

// ===== مدير اتصال Libvirt =====
class LibvirtConnection {
private:
    virConnectPtr conn{nullptr};
    
public:
    explicit LibvirtConnection(const std::string& uri = "qemu:///system") {
        conn = virConnectOpen(uri.c_str());
        if (!conn) {
            throw LibvirtException("Failed to connect to libvirt", -1);
        }
        std::println("✅ Libvirt connection established to: {}", uri);
    }
    
    ~LibvirtConnection() {
        if (conn) {
            virConnectClose(conn);
            std::println("🔌 Libvirt connection closed");
        }
    }
    
    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;
    
    LibvirtConnection(LibvirtConnection&& other) noexcept : conn(other.conn) {
        other.conn = nullptr;
    }
    
    LibvirtConnection& operator=(LibvirtConnection&& other) noexcept {
        if (this != &other) {
            if (conn) virConnectClose(conn);
            conn = other.conn;
            other.conn = nullptr;
        }
        return *this;
    }
    
    virConnectPtr get() const { return conn; }
};

// ===== مدير CGroup =====
class CGroupManager {
private:
    fs::path cgroupPath;
    std::vector<pid_t> managedProcesses;
    std::mutex processesMutex;
    
public:
    explicit CGroupManager(std::string_view name) 
        : cgroupPath(std::format("/sys/fs/cgroup/{}", name)) {
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
    }
    
    void setIOLimit(std::string_view device, uint64_t read_bps, uint64_t write_bps) {
        writeValue("io.max", std::format("{} rbps={} wbps={}", device, read_bps, write_bps));
    }
    
    void addProcess(pid_t pid) {
        writeValue("cgroup.procs", std::to_string(pid));
        
        std::lock_guard lock(processesMutex);
        managedProcesses.push_back(pid);
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
                removeProcess(pid);
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
    
    void removeProcess(pid_t pid) {
        writeValue("cgroup.procs", std::to_string(pid));
    }
    
    bool isCGroupEmpty() const {
        auto procsFile = cgroupPath / "cgroup.procs";
        if (!fs::exists(procsFile)) return true;
        
        std::ifstream file(procsFile);
        return file.peek() == std::ifstream::traits_type::eof();
    }
};

// ===== نظام المراقبة في الوقت الحقيقي =====
class RealTimeMonitor {
private:
    std::shared_ptr<LibvirtConnection> libvirt_conn;
    std::unordered_map<std::string, VMMetrics> vm_metrics;
    HostMetrics host_metrics;
    std::atomic<bool> monitoring_active{false};
    std::jthread monitoring_thread;
    std::mutex metrics_mutex;
    std::vector<std::function<void(const VMMetrics&)>> metrics_callbacks;
    std::vector<std::function<void(const HostMetrics&)>> host_metrics_callbacks;
    
public:
    explicit RealTimeMonitor(std::shared_ptr<LibvirtConnection> conn) 
        : libvirt_conn(conn) {
        vm_metrics.reserve(20);
        metrics_callbacks.reserve(5);
        host_metrics_callbacks.reserve(3);
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
        
        virDomainPtr* domains = nullptr;
        int num_domains = virConnectListAllDomains(libvirt_conn->get(), &domains, 
                                                  VIR_CONNECT_LIST_DOMAINS_ACTIVE);
        
        if (num_domains < 0) {
            throw std::runtime_error("Failed to get domain list");
        }
        
        for (int i = 0; i < num_domains; ++i) {
            virDomainPtr domain = domains[i];
            const char* name = virDomainGetName(domain);
            
            if (!name) continue;
            
            std::string vm_name(name);
            VMMetrics metrics;
            metrics.vm_name = vm_name;
            
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
            virDomainFree(domain);
        }
        
        free(domains);
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
    
public:
    explicit AutoScalingEngine(std::shared_ptr<RealTimeMonitor> mon) 
        : monitor(mon) {
        
        decision_callbacks.reserve(5);
        decision_history.reserve(20);
        
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
    
    void set_scaling_thresholds(double cpu_up, double cpu_down, double mem_up, double mem_down) {
        cpu_scale_up_threshold = cpu_up;
        cpu_scale_down_threshold = cpu_down;
        mem_scale_up_threshold = mem_up;
        mem_scale_down_threshold = mem_down;
    }
    
    std::vector<ScalingDecision> get_decision_history(const std::string& vm_name) {
        std::lock_guard lock(decision_mutex);
        auto it = decision_history.find(vm_name);
        return it != decision_history.end() ? it->second : std::vector<ScalingDecision>{};
    }
    
private:
    void decision_loop(std::stop_token st) {
        while (!st.stop_requested() && scaling_active) {
            try {
                process_decisions();
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
        
        analyze_cpu_usage(metrics, decision);
        analyze_memory_usage(metrics, decision);
        
        if (decision.action != ScalingAction::Maintain) {
            std::lock_guard lock(decision_mutex);
            decision_queue.push(decision);
            
            decision_history[metrics.vm_name].push_back(decision);
            
            if (decision_history[metrics.vm_name].size() > 1000) {
                decision_history[metrics.vm_name].erase(
                    decision_history[metrics.vm_name].begin(),
                    decision_history[metrics.vm_name].begin() + 100
                );
            }
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
        }
        else if (current_cpu < cpu_scale_down_threshold && avg_5min < cpu_scale_down_threshold + 5) {
            decision.action = ScalingAction::ScaleDown;
            decision.resource = ResourceType::CPU;
            decision.amount = calculate_cpu_decrease(limit, current_cpu);
            decision.confidence = calculate_confidence(current_cpu, avg_5min);
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
        }
        else if (memory_usage_percent < mem_scale_down_threshold && 
                avg_5min < mem_scale_down_threshold + 5 &&
                decision.action == ScalingAction::Maintain) {
            decision.action = ScalingAction::ScaleDown;
            decision.resource = ResourceType::Memory;
            decision.amount = calculate_memory_decrease(limit, memory_usage_percent);
            decision.confidence = calculate_confidence(memory_usage_percent, avg_5min);
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
    
    double calculate_confidence(double current, double average) {
        double diff = std::abs(current - average);
        if (diff < 5) return 0.9;
        if (diff < 10) return 0.7;
        if (diff < 15) return 0.5;
        return 0.3;
    }
    
    void process_decisions() {
        std::lock_guard lock(decision_mutex);
        
        while (!decision_queue.empty()) {
            ScalingDecision decision = decision_queue.front();
            decision_queue.pop();
            
            for (const auto& callback : decision_callbacks) {
                callback(decision);
            }
            
            std::println("📋 Scaling decision: {} {} for VM {} with {:.2f}% confidence",
                scaling_action_to_string(decision.action),
                resource_type_to_string(decision.resource),
                decision.vm_name,
                decision.confidence * 100
            );
        }
    }
    
    std::string scaling_action_to_string(ScalingAction action) {
        switch (action) {
            case ScalingAction::ScaleUp: return "ScaleUp";
            case ScalingAction::ScaleDown: return "ScaleDown";
            case ScalingAction::Maintain: return "Maintain";
            case ScalingAction::Migrate: return "Migrate";
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
    std::jthread execution_thread;
    std::atomic<bool> execution_active{false};
    std::mutex execution_mutex;
    std::queue<ScalingDecision> execution_queue;
    std::condition_variable execution_cv;
    
public:
    explicit DecisionExecutor(std::shared_ptr<RealTimeMonitor> mon)
        : monitor(mon) {
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
        execution_queue.push(decision);
        execution_cv.notify_one();
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
                default:
                    break;
            }
            
            std::println("✅ Executed decision: {} {} for VM {}",
                scaling_action_to_string(decision.action),
                resource_type_to_string(decision.resource),
                decision.vm_name
            );
            
        } catch (const std::exception& e) {
            std::println("❌ Failed to execute decision: {}", e.what());
        }
    }
    
    void scale_up_resource(const ScalingDecision& decision) {
        std::println("📈 Scaling up {} for VM {} to {}",
            resource_type_to_string(decision.resource),
            decision.vm_name,
            decision.amount
        );
        
        // تنفيذ الأوامر الفعلية لزيادة الموارد عبر libvirt
    }
    
    void scale_down_resource(const ScalingDecision& decision) {
        std::println("📉 Scaling down {} for VM {} to {}",
            resource_type_to_string(decision.resource),
            decision.vm_name,
            decision.amount
        );
        
        // تنفيذ الأوامر الفعلية لتقليل الموارد
    }
    
    void migrate_vm(const ScalingDecision& decision) {
        std::println("🔄 Migrating VM {} to another host", decision.vm_name);
        
        // تنفيذ هجرة VM إلى مضيف آخر
    }
    
    std::string scaling_action_to_string(ScalingAction action) {
        switch (action) {
            case ScalingAction::ScaleUp: return "ScaleUp";
            case ScalingAction::ScaleDown: return "ScaleDown";
            case ScalingAction::Maintain: return "Maintain";
            case ScalingAction::Migrate: return "Migrate";
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
    std::shared_ptr<RealTimeMonitor> monitor;
    std::shared_ptr<AutoScalingEngine> scaling_engine;
    std::shared_ptr<DecisionExecutor> decision_executor;
    std::atomic<bool> system_active{false};
    
public:
    IntelligentScalingSystem() {
        std::println("🎮 Initializing IntelligentScalingSystem...");
        
        libvirt_conn = std::make_shared<LibvirtConnection>();
        monitor = std::make_shared<RealTimeMonitor>(libvirt_conn);
        scaling_engine = std::make_shared<AutoScalingEngine>(monitor);
        decision_executor = std::make_shared<DecisionExecutor>(monitor);
        
        setup_subscriptions();
        
        std::println("✅ IntelligentScalingSystem fully initialized");
    }
    
    ~IntelligentScalingSystem() {
        stop();
    }
    
    void start() {
        if (system_active) return;
        
        system_active = true;
        monitor->start();
        scaling_engine->start();
        decision_executor->start();
        
        std::println("🚀 Intelligent scaling system started");
    }
    
    void stop() {
        if (!system_active) return;
        
        system_active = false;
        decision_executor->stop();
        scaling_engine->stop();
        monitor->stop();
        
        std::println("🛑 Intelligent scaling system stopped");
    }
    
    void configure_scaling(const std::string& vm_name, const std::vector<ResourceLimit>& limits) {
        scaling_engine->set_resource_limits(vm_name, limits);
    }
    
    void set_scaling_thresholds(double cpu_up, double cpu_down, double mem_up, double mem_down) {
        scaling_engine->set_scaling_thresholds(cpu_up, cpu_down, mem_up, mem_down);
    }
    
    std::vector<ScalingDecision> get_decisions(const std::string& vm_name) {
        return scaling_engine->get_decision_history(vm_name);
    }
    
    void add_virtual_machine(const VMConfig& config) {
        // تنفيذ إضافة جهاز افتراضي
        std::println("🖥️ Added virtual machine: {}", config.name);
    }
    
private:
    void setup_subscriptions() {
        scaling_engine->register_decision_callback([this](const ScalingDecision& decision) {
            decision_executor->schedule_execution(decision);
        });
    }
};

// ===== الدالة الرئيسية =====
int main() {
    try {
        std::println("🚀 Starting Virtual Resource Manager...");
        
        IntelligentScalingSystem system;
        
        // تكوين إعدادات التوازن
        system.set_scaling_thresholds(75.0, 25.0, 80.0, 35.0);
        
        // تكوين موارد VM
        std::vector<ResourceLimit> vm_limits = {
            {ResourceType::CPU, 1, 16, 2, "cores"},
            {ResourceType::Memory, 1024 * 1024 * 1024, 16 * 1024 * 1024 * 1024, 
             2 * 1024 * 1024 * 1024, "bytes"}
        };
        
        system.configure_scaling("ubuntu-vm", vm_limits);
        
        // بدء النظام
        system.start();
        
        std::println("✅ System started successfully!");
        std::println("Press Ctrl+C to exit...");
        
        // الانتظار لإشارة الإيقاف
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock lock(mtx);
        cv.wait(lock);
        
        // إيقاف النظام
        system.stop();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::println("❌ Fatal error: {}", e.what());
        return 1;
    }
}