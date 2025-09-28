// BoostLogger.h
#pragma once

#include <string>
#include <memory>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/shared_ptr.hpp>
#include <iostream>
#include <fstream>

// لدعم التنسيق الحديث (اختياري - يتطلب fmt)
// #include <fmt/format.h>
// #include <fmt/ostream.h>

namespace bl = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace expr = boost::log::expressions;
namespace attrs = boost::log::attributes;
using namespace boost::log::trivial;

class BoostLogger {
public:
    enum class Level {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        fatal = 5
    };

    struct Config {
        std::string name = "app";
        std::string file_path = "logs/app.log";
        Level console_level = Level::Info;
        Level file_level = Level::Trace;
        std::size_t rotation_size = 10 * 1024 * 1024; // 10 MB
        int max_files = 5;
        bool enable_console = true;
        bool enable_file = true;
    };

    // تهيئة السجل وفق الإعدادات
    static void Init(const Config& config = Config());

    // دوال التسجيل الأساسية (تدعم <<)
    static void Trace(const auto& msg) { log_impl(severity_level::trace, msg); }
    static void Debug(const auto& msg) { log_impl(severity_level::debug, msg); }
    static void Info(const auto& msg) { log_impl(severity_level::info, msg); }
    static void Warn(const auto& msg) { log_impl(severity_level::warning, msg); }
    static void Error(const auto& msg) { log_impl(severity_level::error, msg); }
    static void Critical(const auto& msg) { log_impl(severity_level::critical, msg); }

    // دعم التنسيق باستخدام fmt (اختياري - انظر التعليق أدناه)
    /*
    template<typename... Args>
    static void Trace(fmt::format_string<Args...> fmt, Args&&... args) {
        Trace(fmt::format(fmt, std::forward<Args>(args)...));
    }
    // كرر لكل مستوى...
    */

private:
    inline static src::severity_logger_mt<severity_level> s_logger;
    inline static bool s_initialized = false;

    static severity_level to_boost_level(Level level);
    static void log_impl(severity_level lvl, const auto& msg);
};

// تنفيذ الدوال
inline severity_level BoostLogger::to_boost_level(Level level) {
    switch (level) {
        case Level::Trace:    return severity_level::trace;
        case Level::Debug:    return severity_level::debug;
        case Level::Info:     return severity_level::info;
        case Level::Warning:  return severity_level::warning;
        case Level::Error:    return severity_level::error;
        case Level::fatal: return severity_level::fatal;
        default:              return severity_level::info;
    }
}

inline void BoostLogger::log_impl(severity_level lvl, const auto& msg) {
    if (!s_initialized) {
        // تهيئة افتراضية إذا لم تتم التهيئة يدويًا
        Init();
    }
    BOOST_LOG_SEV(s_logger, lvl) << msg;
}

inline void BoostLogger::Init(const Config& config) {
    if (s_initialized) return;

    // مسح أي تسجيلات سابقة (لتجنب التكرار عند إعادة التهيئة)
    bl::core::get()->remove_all_sinks();

    // إضافة سمة الوقت والخطوة (thread id)
    bl::add_common_attributes();

    // sink للكونسول
    if (config.enable_console) {
        auto console_sink = bl::add_console_log(
            std::clog,
            bl::keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%] %Message%"
        );
        console_sink->set_filter(severity >= to_boost_level(config.console_level));
    }

    // sink للملف
    if (config.enable_file) {
        auto file_sink = bl::add_file_log(
            bl::keywords::file_name = config.file_path,
            bl::keywords::rotation_size = config.rotation_size,
            bl::keywords::max_size = config.rotation_size * config.max_files,
            bl::keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%] %Message%",
            bl::keywords::auto_flush = true
        );
        file_sink->set_filter(severity >= to_boost_level(config.file_level));
    }

    // ضبط الحد الأدنى لمستوى التسجيل العام
    bl::core::get()->set_filter(severity >= severity_level::trace);

    s_initialized = true;
}