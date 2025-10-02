#pragma once
#include <boost/asio.hpp>
#include <functional>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace CONCURRENCY {
namespace asio = boost::asio;
using namespace std::chrono_literals;

class Timer {
public:
    Timer(asio::io_context& io, std::chrono::steady_clock::duration dur, std::function<void()> cb);
    void cancel();
    ~Timer();

    // non-copyable, but shareable via shared_ptr<Timer>
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class EventDispatcher {
public:
    explicit EventDispatcher(size_t threads = std::thread::hardware_concurrency());
    ~EventDispatcher();

    // Post immediate task
    void dispatch(std::function<void()> f);

    // Post delayed task (returns shared_ptr to Timer to allow cancel)
    std::shared_ptr<Timer> dispatch_delayed(std::chrono::steady_clock::duration dur, std::function<void()> f);

    // Control lifecycle
    void start();
    void stop();

    // non-copyable
    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace CONCURRENCY
        stop();
    }

    // Post immediate task
    void dispatch(std::function<void()> f) {
        if (!f) return;
        asio::post(io_ctx_, std::move(f));
    }

    // Post delayed task (returns shared_ptr to Timer to allow cancel)
    std::shared_ptr<Timer> dispatch_delayed(std::chrono::steady_clock::duration dur, std::function<void()> f) {
        if (!f) return nullptr;
        auto t = std::make_shared<Timer>(io_ctx_, dur, std::move(f));
        return t;
    }

    void start() {
        if (running_.exchange(true)) return;
        for (size_t i = 0; i < thread_count_; ++i) {
            threads_.emplace_back([this]() {
                try {
                    io_ctx_.run();
                } catch (...) {
                    // ensure thread doesn't propagate exceptions
                }
            });
        }
    }

    void stop() {
        if (!running_.exchange(false)) return;
        work_guard_.reset();
        io_ctx_.stop();
        for (auto &t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

private:
    asio::io_context io_ctx_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;
    size_t thread_count_{1};
    std::atomic<bool> running_;
};
} // namespace CONCURRENCY
