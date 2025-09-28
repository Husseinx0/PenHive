#pragma once
#include <memory>
#include <mutex>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

class SafeLogger {
    static std::shared_ptr<spdlog::logger> instance;
    static std::mutex mtx;

public:
    static void initialize() {
        std::lock_guard lock(mtx);
        if (instance) return;

        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_level(spdlog::level::info);

        auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/virtorch.log", 1024*1024*5, 3);
        file->set_level(spdlog::level::trace);

        instance = std::make_shared<spdlog::logger>("virtorch", spdlog::sinks_init_list{console, file});
        instance->set_level(spdlog::level::trace);
        instance->flush_on(spdlog::level::info);
        spdlog::set_default_logger(instance);
    }

    static std::shared_ptr<spdlog::logger>& get() {
        if (!instance) initialize();
        return instance;
    }
};

#define VLOG_TRACE(...)   SafeLogger::get()->trace(__VA_ARGS__)
#define VLOG_DEBUG(...)   SafeLogger::get()->debug(__VA_ARGS__)
#define VLOG_INFO(...)    SafeLogger::get()->info(__VA_ARGS__)
#define VLOG_WARN(...)    SafeLogger::get()->warn(__VA_ARGS__)
#define VLOG_ERROR(...)   SafeLogger::get()->error(__VA_ARGS__)
#define VLOG_CRITICAL(...) SafeLogger::get()->critical(__VA_ARGS__)

// Initialize static members
std::shared_ptr<spdlog::logger> SafeLogger::instance = nullptr;
std::mutex SafeLogger::mtx;