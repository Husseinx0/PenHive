#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include <expected>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <format>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <concepts>
#include <type_traits>
#include <system_error>

namespace fs = std::filesystem;

// ========================================================
// تعريفات أساسية وثوابت النظام
// ========================================================

/// أنواع الأخطاء المخصصة للنظام
enum class VirtualizationError {
    Success = 0,
    ConnectionFailed,
    DomainNotFound,
    InvalidState,
    ResourceExhausted,
    ConfigurationError,
    PermissionDenied,
    OperationTimeout,
    InternalError
};

/// فئة لتحويل أخطاء libvirt إلى نظام أخطاء موحد
class LibvirtErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { 
        return "libvirt"; 
    }
    
    std::string message(int condition) const override {
        virErrorNumber errNum = static_cast<virErrorNumber>(condition);
        return virGetErrorString(errNum, nullptr);
    }
};

inline const std::error_category& libvirt_category() {
    static LibvirtErrorCategory category;
    return category;
}

inline std::error_code make_error_code(VirtualizationError e) {
    return {static_cast<int>(e), libvirt_category()};
}

namespace std {
    template <>
    struct is_error_code_enum<VirtualizationError> : true_type {};
}

// ========================================================
// نظام تسجيل متقدم (Logger)
// ========================================================

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

class Logger {
public:
    virtual ~Logger() = default;
    
    virtual void log(LogLevel level, 
                    std::string_view message,
                    const std::source_location& location = std::source_location::current()) = 0;
    
    static std::shared_ptr<Logger> createDefault();
};

class ConsoleLogger : public Logger {
    std::mutex mutex_;
    
public:
    void log(LogLevel level, std::string_view message, 
            const std::source_location& location) override {
        std::lock_guard lock(mutex_);
        auto time = std::chrono::system_clock::now();
        auto time_c = std::chrono::system_clock::to_time_t(time);
        
        const char* level_str = [&] {
            switch (level) {
                case LogLevel::Debug: return "DEBUG";
                case LogLevel::Info: return "INFO";
                case LogLevel::Warning: return "WARNING";
                case LogLevel::Error: return "ERROR";
                case LogLevel::Critical: return "CRITICAL";
                default: return "UNKNOWN";
            }
        }();
        
        std::cerr << std::put_time(std::localtime(&time_c), "%Y-%m-%d %H:%M:%S")
                  << " [" << level_str << "] "
                  << location.file_name() << ":" << location.line() << " - "
                  << message << std::endl;
    }
};

std::shared_ptr<Logger> Logger::createDefault() {
    return std::make_shared<ConsoleLogger>();
}

// ========================================================
// إدارة اتصال libvirt مع دعم المصادقة الآمنة
// ========================================================

class LibvirtConnection {
    virConnectPtr conn_ = nullptr;
    std::shared_ptr<Logger> logger_;
    std::string uri_;

    // RAII wrapper for virError
    struct VirErrorGuard {
        VirErrorGuard() { virSetErrorFunc(nullptr, nullptr); }
        ~VirErrorGuard() { virSetErrorFunc(nullptr, nullptr); }
    };

    static int authCallback(virConnectCredentialPtr cred, 
                           unsigned int ncred,
                           void* cbdata,
                           void* /* unused */) noexcept {
        auto* auth = static_cast<AuthHandler*>(cbdata);
        return auth->handleCredentials(cred, ncred);
    }

    explicit LibvirtConnection(virConnectPtr conn, 
                              std::shared_ptr<Logger> logger,
                              std::string uri) noexcept
        : conn_(conn), logger_(std::move(logger)), uri_(std::move(uri)) {}

public:
    struct AuthHandler {
        virtual ~AuthHandler() = default;
        virtual int handleCredentials(virConnectCredentialPtr cred, 
                                     unsigned int ncred) const noexcept = 0;
    };

    struct DefaultAuthHandler : AuthHandler {
        std::string username;
        std::string password;
        
        explicit DefaultAuthHandler(std::string user, std::string pass) 
            : username(std::move(user)), password(std::move(pass)) {}
        
        int handleCredentials(virConnectCredentialPtr cred, 
                             unsigned int ncred) const noexcept override {
            for (unsigned int i = 0; i < ncred; ++i) {
                switch (cred[i].type) {
                    case VIR_CRED_USERNAME:
                        cred[i].result = strdup(username.c_str());
                        cred[i].resultlen = username.size();
                        break;
                    case VIR_CRED_PASSPHRASE:
                        cred[i].result = strdup(password.c_str());
                        cred[i].resultlen = password.size();
                        break;
                    default:
                        return -1;
                }
            }
            return 0;
        }
    };

    static std::expected<LibvirtConnection, std::error_code> connect(
        const std::string& uri = "qemu:///system",
        std::shared_ptr<Logger> logger = Logger::createDefault(),
        const AuthHandler* auth = nullptr) noexcept {
        
        VirErrorGuard errorGuard;
        virConnectPtr conn = nullptr;
        
        try {
            if (auth) {
                virConnectAuth cauth = {
                    .credtype = const_cast<int*>(authCredTypes.data()),
                    .ncredtype = static_cast<unsigned int>(authCredTypes.size()),
                    .cb = authCallback,
                    .cbdata = const_cast<AuthHandler*>(auth)
                };
                
                conn = virConnectOpenAuth(uri.c_str(), &cauth, 0);
            } else {
                conn = virConnectOpen(uri.c_str());
            }
            
            if (!conn) {
                auto err = getLibvirtError();
                logger->log(LogLevel::Error, 
                           std::format("Connection failed: {}", err.message));
                return std::unexpected(make_error_code(VirtualizationError::ConnectionFailed));
            }
            
            return LibvirtConnection(conn, std::move(logger), uri);
        } catch (...) {
            return std::unexpected(make_error_code(VirtualizationError::InternalError));
        }
    }

    // منع النسخ
    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;

    // دعم نقل الموارد
    LibvirtConnection(LibvirtConnection&& other) noexcept
        : conn_(std::exchange(other.conn_, nullptr)),
          logger_(std::move(other.logger_)),
          uri_(std::move(other.uri_)) {}
    
    LibvirtConnection& operator=(LibvirtConnection&& other) noexcept {
        if (this != &other) {
            reset();
            conn_ = std::exchange(other.conn_, nullptr);
            logger_ = std::move(other.logger_);
            uri_ = std::move(other.uri_);
        }
        return *this;
    }

    ~LibvirtConnection() {
        reset();
    }

    [[nodiscard]] virConnectPtr get() const noexcept { 
        return conn_; 
    }
    
    [[nodiscard]] const std::string& getUri() const noexcept {
        return uri_;
    }
    
    [[nodiscard]] bool isAlive() const noexcept {
        return conn_ && virConnectIsAlive(conn_) == 1;
    }

    [[nodiscard]] std::expected<std::string, std::error_code> getHypervisorVersion() const noexcept {
        unsigned long version;
        if (virConnectGetVersion(conn_, &version) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        return std::format("{}.{}.{}", 
                          (version >> 16) & 0xFF,
                          (version >> 8) & 0xFF,
                          version & 0xFF);
    }

    [[nodiscard]] std::expected<uint64_t, std::error_code> getMaxMemory() const noexcept {
        auto memory = virNodeGetInfo(conn_, nullptr);
        if (memory == 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return memory;
    }

private:
    static constexpr std::array<int, 2> authCredTypes = {
        VIR_CRED_USERNAME, VIR_CRED_PASSPHRASE
    };
    
    void reset() noexcept {
        if (conn_) {
            virConnectClose(conn_);
            conn_ = nullptr;
        }
    }

    [[nodiscard]] static LibvirtError getLibvirtError() noexcept {
        virErrorPtr err = virGetLastError();
        if (err) {
            std::string message = err->message ? err->message : "Unknown error";
            virErrorNumber code = err->code;
            virResetError(err);
            return {static_cast<int>(code), std::move(message)};
        }
        return {static_cast<int>(VIR_ERR_OK), "Success"};
    }

    struct LibvirtError {
        int code;
        std::string message;
        
        [[nodiscard]] std::error_code toErrorCode() const noexcept {
            return std::error_code(code, libvirt_category());
        }
    };
};

// ========================================================
// فئة إدارة النطاقات (الأجهزة الافتراضية)
// ========================================================

class DomainHandle {
    virDomainPtr domain_ = nullptr;
    std::shared_ptr<Logger> logger_;

    explicit DomainHandle(virDomainPtr domain, std::shared_ptr<Logger> logger) noexcept
        : domain_(domain), logger_(std::move(logger)) {}

public:
    static std::expected<DomainHandle, std::error_code> lookupByName(
        virConnectPtr conn, 
        const std::string& name,
        std::shared_ptr<Logger> logger) noexcept {
        
        virDomainPtr domain = virDomainLookupByName(conn, name.c_str());
        if (!domain) {
            auto err = LibvirtConnection::LibvirtError::getLibvirtError();
            logger->log(LogLevel::Error, 
                       std::format("Domain '{}' not found: {}", name, err.message));
            return std::unexpected(err.toErrorCode());
        }
        return DomainHandle(domain, std::move(logger));
    }

    static std::expected<DomainHandle, std::error_code> createFromXML(
        virConnectPtr conn, 
        const std::string& xmlDesc,
        unsigned int flags,
        std::shared_ptr<Logger> logger) noexcept {
        
        virDomainPtr domain = virDomainCreateXML(conn, xmlDesc.c_str(), flags);
        if (!domain) {
            auto err = LibvirtConnection::LibvirtError::getLibvirtError();
            logger->log(LogLevel::Error, 
                       std::format("Failed to create domain: {}", err.message));
            return std::unexpected(err.toErrorCode());
        }
        return DomainHandle(domain, std::move(logger));
    }

    static std::expected<DomainHandle, std::error_code> defineFromXML(
        virConnectPtr conn, 
        const std::string& xmlDesc,
        std::shared_ptr<Logger> logger) noexcept {
        
        virDomainPtr domain = virDomainDefineXML(conn, xmlDesc.c_str());
        if (!domain) {
            auto err = LibvirtConnection::LibvirtError::getLibvirtError();
            logger->log(LogLevel::Error, 
                       std::format("Failed to define domain: {}", err.message));
            return std::unexpected(err.toErrorCode());
        }
        return DomainHandle(domain, std::move(logger));
    }

    // منع النسخ
    DomainHandle(const DomainHandle&) = delete;
    DomainHandle& operator=(const DomainHandle&) = delete;

    // دعم نقل الموارد
    DomainHandle(DomainHandle&& other) noexcept
        : domain_(std::exchange(other.domain_, nullptr)),
          logger_(std::move(other.logger_)) {}
    
    DomainHandle& operator=(DomainHandle&& other) noexcept {
        if (this != &other) {
            reset();
            domain_ = std::exchange(other.domain_, nullptr);
            logger_ = std::move(other.logger_);
        }
        return *this;
    }

    ~DomainHandle() {
        reset();
    }

    [[nodiscard]] virDomainPtr get() const noexcept { 
        return domain_; 
    }

    [[nodiscard]] std::expected<std::string, std::error_code> getName() const noexcept {
        char* name = virDomainGetName(domain_);
        if (!name) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        std::string result(name);
        virFree(name);
        return result;
    }

    [[nodiscard]] std::expected<int, std::error_code> getID() const noexcept {
        int id = virDomainGetID(domain_);
        if (id == -1) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return id;
    }

    [[nodiscard]] std::expected<bool, std::error_code> isActive() const noexcept {
        int active = virDomainIsActive(domain_);
        if (active < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return active == 1;
    }

    [[nodiscard]] std::expected<void, std::error_code> destroy() const noexcept {
        if (virDomainDestroy(domain_) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> shutdown() const noexcept {
        if (virDomainShutdown(domain_) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> reboot() const noexcept {
        if (virDomainReboot(domain_, 0) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> suspend() const noexcept {
        if (virDomainSuspend(domain_) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> resume() const noexcept {
        if (virDomainResume(domain_) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<std::string, std::error_code> getXMLDesc(int flags = 0) const noexcept {
        char* xml = virDomainGetXMLDesc(domain_, flags);
        if (!xml) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        std::string result(xml);
        virFree(xml);
        return result;
    }

    [[nodiscard]] std::expected<uint64_t, std::error_code> getMaxMemory() const noexcept {
        auto mem = virDomainGetMaxMemory(domain_);
        if (mem == 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return mem;
    }

    [[nodiscard]] std::expected<int, std::error_code> getVcpus() const noexcept {
        virDomainInfo info;
        if (virDomainGetInfo(domain_, &info) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return info.nrVirtCpu;
    }

    [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> 
    listSnapshots() const noexcept {
        
        int num = virDomainSnapshotNum(domain_, 0);
        if (num < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        std::vector<char*> names(num);
        if (virDomainSnapshotListNames(domain_, names.data(), num, 0) != num) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        std::vector<std::string> result;
        result.reserve(num);
        for (int i = 0; i < num; ++i) {
            result.emplace_back(names[i]);
            virFree(names[i]);
        }
        return result;
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    createSnapshot(const std::string& xmlDesc, unsigned int flags = 0) const noexcept {
        
        if (!virDomainSnapshotCreateXML(domain_, xmlDesc.c_str(), flags)) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    revertToSnapshot(const std::string& snapshotName, unsigned int flags = 0) const noexcept {
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain_, snapshotName.c_str(), 0);
        if (!snapshot) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        int result = virDomainRevertToSnapshot(snapshot, flags);
        virDomainSnapshotFree(snapshot);
        
        if (result < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    deleteSnapshot(const std::string& snapshotName, unsigned int flags = 0) const noexcept {
        
        virDomainSnapshotPtr snapshot = virDomainSnapshotLookupByName(domain_, snapshotName.c_str(), 0);
        if (!snapshot) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        int result = virDomainSnapshotDelete(snapshot, flags);
        virDomainSnapshotFree(snapshot);
        
        if (result < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

private:
    void reset() noexcept {
        if (domain_) {
            virDomainFree(domain_);
            domain_ = nullptr;
        }
    }

    [[nodiscard]] LibvirtConnection::LibvirtError getLibvirtError() const noexcept {
        virErrorPtr err = virGetLastError();
        if (err) {
            std::string message = err->message ? err->message : "Unknown error";
            int code = err->code;
            virResetError(err);
            return {code, std::move(message)};
        }
        return {-1, "No libvirt error available"};
    }
};

// ========================================================
// مُنشئ تكوين الجهاز الافتراضي (VMConfigBuilder)
// ========================================================

class VMConfigBuilder {
    std::string name_;
    uint64_t memory_ = 1024; // MiB
    int vcpus_ = 2;
    std::string osType_ = "hvm";
    std::string arch_ = "x86_64";
    std::string emulator_ = "/usr/bin/qemu-system-x86_64";
    std::vector<std::string> disks_;
    std::vector<std::string> networks_;
    bool hasCloudInit_ = false;
    std::optional<std::string> cloudInitUserData_;
    std::optional<std::string> cloudInitMetaData_;

public:
    VMConfigBuilder& setName(std::string name) {
        name_ = std::move(name);
        return *this;
    }

    VMConfigBuilder& setMemory(uint64_t sizeMiB) {
        memory_ = sizeMiB;
        return *this;
    }

    VMConfigBuilder& setVcpus(int count) {
        vcpus_ = count;
        return *this;
    }

    VMConfigBuilder& setOsType(std::string type) {
        osType_ = std::move(type);
        return *this;
    }

    VMConfigBuilder& setArch(std::string arch) {
        arch_ = std::move(arch);
        return *this;
    }

    VMConfigBuilder& setEmulator(std::string emulator) {
        emulator_ = std::move(emulator);
        return *this;
    }

    VMConfigBuilder& addDisk(std::string source, 
                           std::string target = "vda",
                           std::string device = "disk",
                           std::string type = "file",
                           std::string format = "qcow2") {
        disks_.emplace_back(std::format(R"(
        <disk type='{}' device='{}'>
          <driver name='qemu' type='{}'/>
          <source file='{}'/>
          <target dev='{}' bus='virtio'/>
        </disk>)", type, device, format, source, target));
        return *this;
    }

    VMConfigBuilder& addNetwork(std::string networkName = "default") {
        networks_.emplace_back(std::format(R"(
        <interface type='network'>
          <source network='{}'/>
          <model type='virtio'/>
        </interface>)", networkName));
        return *this;
    }

    VMConfigBuilder& enableCloudInit(std::string userData, std::string metaData = "") {
        hasCloudInit_ = true;
        cloudInitUserData_ = std::move(userData);
        cloudInitMetaData_ = std::move(metaData);
        return *this;
    }

    [[nodiscard]] std::string build() const {
        std::string disks;
        for (const auto& disk : disks_) {
            disks += disk;
        }
        
        std::string networks;
        for (const auto& network : networks_) {
            networks += network;
        }
        
        std::string cloudInit;
        if (hasCloudInit_) {
            cloudInit = R"(
        <qemu:commandline>
          <qemu:arg value='-fw_cfg'/>
          <qemu:arg value='name=opt/com.coreos/config,file=/tmp/user-data'/>
        </qemu:commandline>)";
        }
        
        return std::format(R"(
<domain type='kvm'>
  <name>{}</name>
  <memory unit='MiB'>{}</memory>
  <vcpu placement='static'>{}</vcpu>
  <os>
    <type arch='{}' machine='q35'>{}</type>
    <boot dev='hd'/>
  </os>
  <features>
    <acpi/>
    <apic/>
  </features>
  <cpu mode='host-passthrough' check='none'/>
  <clock offset='utc'/>
  <on_poweroff>destroy</on_poweroff>
  <on_reboot>restart</on_reboot>
  <on_crash>restart</on_crash>
  <devices>
    <emulator>{}</emulator>
    {}
    {}
    <graphics type='spice' autoport='yes'>
      <listen type='address'/>
    </graphics>
    <video>
      <model type='qxl'/>
    </video>
    <memballoon model='virtio'/>
    {}
  </devices>
</domain>)", 
        name_, memory_, vcpus_, arch_, osType_, emulator_, 
        disks, networks, cloudInit);
    }

    [[nodiscard]] static std::expected<VMConfigBuilder, std::error_code> 
    fromExistingDomain(virConnectPtr conn, const std::string& domainName) {
        auto domain = DomainHandle::lookupByName(conn, domainName, Logger::createDefault());
        if (!domain) {
            return std::unexpected(domain.error());
        }
        
        auto xml = domain->getXMLDesc();
        if (!xml) {
            return std::unexpected(xml.error());
        }
        
        // هنا يمكننا تحليل XML واستخراج الإعدادات
        // هذا مثال مبسط، في النظام الحقيقي نستخدم مكتبة XML
        VMConfigBuilder builder;
        builder.setName(domainName);
        
        // في تطبيق حقيقي، نستخدم libvirt XML parser للحصول على القيم الفعلية
        builder.setMemory(1024);
        builder.setVcpus(2);
        
        return builder;
    }
};

// ========================================================
// فئة الجهاز الافتراضي (واجهة عالية المستوى)
// ========================================================

class VirtualMachine {
    std::string name_;
    DomainHandle domain_;
    std::shared_ptr<Logger> logger_;
    std::atomic<bool> isMonitoring_{false};
    std::thread monitorThread_;

    void startMonitoring() {
        isMonitoring_ = true;
        monitorThread_ = std::thread([this] {
            while (isMonitoring_) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                
                auto state = getState();
                if (!state) {
                    logger_->log(LogLevel::Error, 
                                std::format("Failed to get state for {}: {}", 
                                           name_, state.error().message()));
                    continue;
                }
                
                logger_->log(LogLevel::Debug, 
                            std::format("VM {} state: {}", name_, stateToString(*state)));
                
                if (*state == VIR_DOMAIN_SHUTOFF) {
                    logger_->log(LogLevel::Info, 
                                std::format("VM {} has shut off", name_));
                    // يمكن هنا تنفيذ إجراءات ما بعد الإيقاف
                }
            }
        });
    }

public:
    VirtualMachine(std::string name, 
                  DomainHandle domain,
                  std::shared_ptr<Logger> logger) noexcept
        : name_(std::move(name)), 
          domain_(std::move(domain)),
          logger_(std::move(logger)) {
        
        startMonitoring();
    }

    VirtualMachine(VirtualMachine&&) noexcept = default;
    VirtualMachine& operator=(VirtualMachine&&) noexcept = default;

    ~VirtualMachine() {
        stopMonitoring();
    }

    [[nodiscard]] const std::string& getName() const noexcept {
        return name_;
    }

    [[nodiscard]] std::expected<void, std::error_code> start() const noexcept {
        if (auto active = domain_.isActive(); active && *active) {
            return {}; // الجاهز يعمل بالفعل
        }
        
        if (virDomainCreate(domain_.get()) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> stop() const noexcept {
        if (auto active = domain_.isActive(); !active || !*active) {
            return {}; // الجهاز متوقف بالفعل
        }
        
        return domain_.destroy();
    }

    [[nodiscard]] std::expected<void, std::error_code> shutdown() const noexcept {
        if (auto active = domain_.isActive(); !active || !*active) {
            return {}; // الجهاز متوقف بالفعل
        }
        
        return domain_.shutdown();
    }

    [[nodiscard]] std::expected<void, std::error_code> reboot() const noexcept {
        if (auto active = domain_.isActive(); !active || !*active) {
            return std::unexpected(make_error_code(VirtualizationError::InvalidState));
        }
        
        return domain_.reboot();
    }

    [[nodiscard]] std::expected<void, std::error_code> suspend() const noexcept {
        if (auto active = domain_.isActive(); !active || !*active) {
            return std::unexpected(make_error_code(VirtualizationError::InvalidState));
        }
        
        return domain_.suspend();
    }

    [[nodiscard]] std::expected<void, std::error_code> resume() const noexcept {
        if (auto active = domain_.isActive(); active && *active) {
            return {}; // الجهاز يعمل بالفعل
        }
        
        return domain_.resume();
    }

    [[nodiscard]] std::expected<int, std::error_code> getState() const noexcept {
        virDomainInfo info;
        if (virDomainGetInfo(domain_.get(), &info) < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        return info.state;
    }

    [[nodiscard]] static std::string stateToString(int state) {
        switch (state) {
            case VIR_DOMAIN_NOSTATE:  return "no state";
            case VIR_DOMAIN_RUNNING:  return "running";
            case VIR_DOMAIN_BLOCKED:  return "blocked";
            case VIR_DOMAIN_PAUSED:   return "paused";
            case VIR_DOMAIN_SHUTDOWN: return "shutdown";
            case VIR_DOMAIN_SHUTOFF:  return "shutoff";
            case VIR_DOMAIN_CRASHED:  return "crashed";
            case VIR_DOMAIN_PMSUSPENDED: return "suspended";
            default: return "unknown";
        }
    }

    [[nodiscard]] std::expected<std::string, std::error_code> getXMLDesc() const noexcept {
        return domain_.getXMLDesc();
    }

    [[nodiscard]] std::expected<uint64_t, std::error_code> getMaxMemory() const noexcept {
        return domain_.getMaxMemory();
    }

    [[nodiscard]] std::expected<int, std::error_code> getVcpus() const noexcept {
        return domain_.getVcpus();
    }

    [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> 
    listSnapshots() const noexcept {
        return domain_.listSnapshots();
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    createSnapshot(const std::string& name, bool persistent = true) const noexcept {
        
        std::string xml = std::format(R"(
        <domainsnapshot>
          <name>{}</name>
          <description>Snapshot created by VM Manager</description>
        </domainsnapshot>)", name);
        
        unsigned int flags = persistent ? VIR_DOMAIN_SNAPSHOT_CREATE_XML_INACTIVE : 0;
        return domain_.createSnapshot(xml, flags);
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    revertToSnapshot(const std::string& name) const noexcept {
        return domain_.revertToSnapshot(name);
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    deleteSnapshot(const std::string& name) const noexcept {
        return domain_.deleteSnapshot(name);
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    clone(const std::string& newName, bool persistent = true) const noexcept {
        
        auto xml = getXMLDesc();
        if (!xml) {
            return std::unexpected(xml.error());
        }
        
        // تعديل XML لإنشاء جهاز مكرر
        std::string newXml = std::regex_replace(*xml, 
            std::regex(R"(<name>.*?</name>)"), 
            std::format("<name>{}</name>", newName));
        
        unsigned int flags = persistent ? VIR_DOMAIN_DEFINE_VALIDATE : 0;
        auto newDomain = DomainHandle::defineFromXML(
            domain_.get()->conn, newXml, Logger::createDefault());
        
        if (!newDomain) {
            return std::unexpected(newDomain.error());
        }
        
        if (persistent) {
            return {};
        }
        
        return newDomain->create();
    }

private:
    void stopMonitoring() {
        isMonitoring_ = false;
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }
    }

    [[nodiscard]] LibvirtConnection::LibvirtError getLibvirtError() const noexcept {
        virErrorPtr err = virGetLastError();
        if (err) {
            std::string message = err->message ? err->message : "Unknown error";
            int code = err->code;
            virResetError(err);
            return {code, std::move(message)};
        }
        return {-1, "No libvirt error available"};
    }
};

// ========================================================
// مدير البيئات الافتراضية (واجهة المستخدم النهائية)
// ========================================================

class VirtualMachineManager {
    LibvirtConnection connection_;
    std::shared_ptr<Logger> logger_;
    std::mutex mutex_;

    explicit VirtualMachineManager(LibvirtConnection conn, 
                                  std::shared_ptr<Logger> logger) noexcept
        : connection_(std::move(conn)), logger_(std::move(logger)) {}

public:
    static std::expected<VirtualMachineManager, std::error_code> create(
        const std::string& uri = "qemu:///system",
        std::shared_ptr<Logger> logger = Logger::createDefault(),
        const LibvirtConnection::AuthHandler* auth = nullptr) noexcept {
        
        auto conn = LibvirtConnection::connect(uri, logger, auth);
        if (!conn) {
            return std::unexpected(conn.error());
        }
        return VirtualMachineManager(std::move(*conn), std::move(logger));
    }

    [[nodiscard]] std::expected<std::vector<VirtualMachine>, std::error_code> 
    listAllVMs() const noexcept {
        
        std::lock_guard lock(mutex_);
        auto conn = connection_.get();
        
        // الحصول على جميع الأجهزة (نشطة وغير نشطة)
        std::vector<VirtualMachine> vms;
        
        // 1. الأجهزة النشطة
        int numActive = virConnectNumOfDomains(conn);
        if (numActive < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (numActive > 0) {
            std::vector<int> activeIDs(numActive);
            if (virConnectListDomains(conn, activeIDs.data(), numActive) != numActive) {
                return std::unexpected(getLibvirtError().toErrorCode());
            }
            
            for (int id : activeIDs) {
                virDomainPtr domain = virDomainLookupByID(conn, id);
                if (!domain) continue;
                
                char* name = virDomainGetName(domain);
                if (name) {
                    vms.emplace_back(
                        name,
                        DomainHandle(domain, logger_),
                        logger_
                    );
                    virFree(name);
                } else {
                    virDomainFree(domain);
                }
            }
        }
        
        // 2. الأجهزة غير النشطة
        int numInactive = virConnectNumOfDefinedDomains(conn);
        if (numInactive < 0) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (numInactive > 0) {
            std::vector<char*> inactiveNames(numInactive);
            if (virConnectListDefinedDomains(conn, 
                inactiveNames.data(), numInactive) != numInactive) {
                return std::unexpected(getLibvirtError().toErrorCode());
            }
            
            for (int i = 0; i < numInactive; ++i) {
                virDomainPtr domain = virDomainLookupByName(conn, inactiveNames[i]);
                if (!domain) {
                    virFree(inactiveNames[i]);
                    continue;
                }
                
                vms.emplace_back(
                    inactiveNames[i],
                    DomainHandle(domain, logger_),
                    logger_
                );
                virFree(inactiveNames[i]);
            }
        }
        
        return vms;
    }

    [[nodiscard]] std::expected<VirtualMachine, std::error_code> 
    getVM(const std::string& name) const noexcept {
        
        std::lock_guard lock(mutex_);
        auto domain = DomainHandle::lookupByName(
            connection_.get(), name, logger_);
        
        if (!domain) {
            return std::unexpected(domain.error());
        }
        
        return VirtualMachine(name, std::move(*domain), logger_);
    }

    [[nodiscard]] std::expected<VirtualMachine, std::error_code> 
    createVM(VMConfigBuilder builder, bool persistent = true) const noexcept {
        
        std::lock_guard lock(mutex_);
        auto xml = builder.build();
        
        if (persistent) {
            auto domain = DomainHandle::defineFromXML(
                connection_.get(), xml, logger_);
            
            if (!domain) {
                return std::unexpected(domain.error());
            }
            
            return VirtualMachine(builder.getName(), std::move(*domain), logger_);
        } else {
            auto domain = DomainHandle::createFromXML(
                connection_.get(), xml, 0, logger_);
            
            if (!domain) {
                return std::unexpected(domain.error());
            }
            
            return VirtualMachine(builder.getName(), std::move(*domain), logger_);
        }
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    deleteVM(const std::string& name, bool removeStorage = false) const noexcept {
        
        std::lock_guard lock(mutex_);
        auto vm = getVM(name);
        if (!vm) {
            return std::unexpected(vm.error());
        }
        
        // إيقاف الجهاز إذا كان يعمل
        if (auto state = vm->getState(); state && *state == VIR_DOMAIN_RUNNING) {
            auto result = vm->stop();
            if (!result) {
                return result;
            }
        }
        
        // حذف النطاق
        virDomainPtr domain = virDomainLookupByName(connection_.get(), name.c_str());
        if (!domain) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (virDomainUndefineFlags(domain, 
            VIR_DOMAIN_UNDEFINE_NVRAM | 
            (removeStorage ? VIR_DOMAIN_UNDEFINE_MANAGED_SAVE : 0)) < 0) {
            
            virDomainFree(domain);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        virDomainFree(domain);
        return {};
    }

    [[nodiscard]] std::expected<std::string, std::error_code> 
    getHypervisorVersion() const noexcept {
        return connection_.getHypervisorVersion();
    }

    [[nodiscard]] std::expected<uint64_t, std::error_code> 
    getSystemMemory() const noexcept {
        return connection_.getMaxMemory();
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    createStoragePool(const std::string& name, 
                     const fs::path& path,
                     bool autostart = true) const noexcept {
        
        std::string xml = std::format(R"(
        <pool type='dir'>
          <name>{}</name>
          <target>
            <path>{}</path>
          </target>
        </pool>)", name, path.string());
        
        virStoragePoolPtr pool = virStoragePoolDefineXML(connection_.get(), xml.c_str(), 0);
        if (!pool) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (virStoragePoolBuild(pool, 0) < 0) {
            virStoragePoolFree(pool);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (autostart && virStoragePoolSetAutostart(pool, 1) < 0) {
            virStoragePoolFree(pool);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (virStoragePoolCreate(pool, 0) < 0) {
            virStoragePoolFree(pool);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        virStoragePoolFree(pool);
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> 
    createNetwork(const std::string& name, 
                 const std::string& subnet,
                 bool dhcp = true,
                 bool autostart = true) const noexcept {
        
        std::string xml = std::format(R"(
        <network>
          <name>{}</name>
          <forward mode='nat'/>
          <bridge name='virbr0'/>
          <ip address='192.168.100.1' netmask='255.255.255.0'>
            {}
          </ip>
        </network>)", 
        name, 
        dhcp ? R"(<dhcp><range start='192.168.100.2' end='192.168.100.254'/></dhcp>)" : "");
        
        virNetworkPtr network = virNetworkDefineXML(connection_.get(), xml.c_str());
        if (!network) {
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (autostart && virNetworkSetAutostart(network, 1) < 0) {
            virNetworkFree(network);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        if (virNetworkCreate(network) < 0) {
            virNetworkFree(network);
            return std::unexpected(getLibvirtError().toErrorCode());
        }
        
        virNetworkFree(network);
        return {};
    }

    [[nodiscard]] std::expected<VMConfigBuilder, std::error_code> 
    getConfigBuilderForExisting(const std::string& name) const noexcept {
        return VMConfigBuilder::fromExistingDomain(connection_.get(), name);
    }

private:
    [[nodiscard]] LibvirtConnection::LibvirtError getLibvirtError() const noexcept {
        virErrorPtr err = virGetLastError();
        if (err) {
            std::string message = err->message ? err->message : "Unknown error";
            int code = err->code;
            virResetError(err);
            return {code, std::move(message)};
        }
        return {-1, "No libvirt error available"};
    }
};

// ========================================================
// مثال تطبيقي متقدم
// ========================================================

namespace vm_demo {
    void runAdvancedDemo() {
        auto logger = Logger::createDefault();
        auto manager = VirtualMachineManager::create("qemu:///system", logger);
        
        if (!manager) {
            logger->log(LogLevel::Critical, 
                       std::format("Failed to connect to hypervisor: {}", 
                                  manager.error().message()));
            return;
        }
        
        logger->log(LogLevel::Info, "Connected to hypervisor successfully");
        
        // 1. عرض معلومات النظام
        if (auto version = manager->getHypervisorVersion(); version) {
            logger->log(LogLevel::Info, std::format("Hypervisor version: {}", *version));
        }
        
        if (auto memory = manager->getSystemMemory(); memory) {
            logger->log(LogLevel::Info, 
                       std::format("Total system memory: {} MB", *memory / 1024));
        }
        
        // 2. إنشاء شبكة افتراضية
        auto networkResult = manager->createNetwork("vm-net", "192.168.100.0/24");
        if (networkResult) {
            logger->log(LogLevel::Info, "Network 'vm-net' created successfully");
        } else if (networkResult.error() != VIR_ERR_OPERATION_INVALID) {
            // قد تكون الشبكة موجودة مسبقًا
            logger->log(LogLevel::Warning, 
                       std::format("Network creation failed: {}", 
                                 networkResult.error().message()));
        }
        
        // 3. إنشاء مجموعة تخزين
        auto poolResult = manager->createStoragePool("vm-storage", "/var/lib/libvirt/images");
        if (poolResult) {
            logger->log(LogLevel::Info, "Storage pool created successfully");
        } else if (poolResult.error() != VIR_ERR_OPERATION_INVALID) {
            logger->log(LogLevel::Warning, 
                       std::format("Storage pool creation failed: {}", 
                                 poolResult.error().message()));
        }
        
        // 4. إنشاء جهاز افتراضي جديد
        VMConfigBuilder builder;
        builder.setName("demo-vm")
               .setMemory(2048)
               .setVcpus(4)
               .addDisk("/var/lib/libvirt/images/demo-vm.qcow2")
               .addNetwork("vm-net");
        
        auto newVM = manager->createVM(builder, true);
        if (newVM) {
            logger->log(LogLevel::Info, "VM 'demo-vm' created successfully");
            
            // 5. تشغيل الجهاز
            if (auto startResult = newVM->start(); startResult) {
                logger->log(LogLevel::Info, "VM started successfully");
                
                // 6. إنشاء لقطة
                if (auto snapResult = newVM->createSnapshot("initial-state"); snapResult) {
                    logger->log(LogLevel::Info, "Snapshot 'initial-state' created");
                } else {
                    logger->log(LogLevel::Error, 
                               std::format("Failed to create snapshot: {}", 
                                         snapResult.error().message()));
                }
                
                // 7. إيقاف الجهاز بعد 10 ثوانٍ
                std::this_thread::sleep_for(std::chrono::seconds(10));
                if (auto stopResult = newVM->shutdown(); stopResult) {
                    logger->log(LogLevel::Info, "VM shutdown initiated");
                }
            } else {
                logger->log(LogLevel::Error, 
                           std::format("Failed to start VM: {}", 
                                     startResult.error().message()));
            }
        } else {
            logger->log(LogLevel::Error, 
                       std::format("Failed to create VM: {}", 
                                 newVM.error().message()));
        }
        
        // 8. عرض جميع الأجهزة
        auto allVMs = manager->listAllVMs();
        if (allVMs) {
            logger->log(LogLevel::Info, 
                       std::format("Found {} virtual machines", allVMs->size()));
            
            for (const auto& vm : *allVMs) {
                auto state = vm.getState();
                logger->log(LogLevel::Info, 
                           std::format("- {} [{}]", 
                                     vm.getName(), 
                                     state ? VirtualMachine::stateToString(*state) : "unknown"));
            }
        }
    }
}