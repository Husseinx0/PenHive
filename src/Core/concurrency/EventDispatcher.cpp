#include "/home/hussin/Desktop/PenHive/include/Core/concurrency/EventDispatcher.hpp"
#include <boost/system/error_code.hpp>
#include <atomic>
#include <iostream>

namespace CONCURRENCY {

//
// Timer::Impl
//
struct Timer::Impl {
    asio::steady_timer timer;
    std::function<void()> cb;
    std::atomic<bool> cancelled;

    Impl(asio::io_context& io, std::chrono::steady_clock::duration dur, std::function<void()> cb_)
        : timer(io, dur), cb(std::move(cb_)), cancelled(false)
    {
        timer.async_wait([self = this](const boost::system::error_code& ec) {
            if (ec || self->cancelled.load()) return;
            try {
                if (self->cb) self->cb();
            } catch (...) {
                // swallow exceptions to avoid terminating io thread
            }
        });
    }

    void cancel() {
        cancelled.store(true);
        boost::system::error_code ec;
        timer.cancel(ec);
    }
};

Timer::Timer(asio::io_context& io, std::chrono::steady_clock::duration dur, std::function<void()> cb)
    : impl_(std::make_shared<Impl>(io, dur, std::move(cb)))
{}

void Timer::cancel() {
    if (impl_) impl_->cancel();
}

Timer::~Timer() {
    cancel();
}

//
// EventDispatcher::Impl
//
struct EventDispatcher::Impl {
    asio::io_context io_ctx;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard;
    std::vector<std::thread> threads;
    size_t thread_count{1};
    std::atomic<bool> running{false};

    explicit Impl(size_t threads_count)
        : io_ctx(), work_guard(asio::make_work_guard(io_ctx)), thread_count(threads_count)
    {}

    void run_threads() {
        if (running.exchange(true)) return;
        for (size_t i = 0; i < thread_count; ++i) {
            threads.emplace_back([this]() {
                try {
                    io_ctx.run();
                } catch (const std::exception& e) {
                    // Log if logger available; avoid throwing from thread
                    (void)e;
                } catch (...) {
                    // swallow
                }
            });
        }
    }

    void stop_threads() {
        if (!running.exchange(false)) return;
        work_guard.reset();
        boost::system::error_code ec;
        io_ctx.stop();
        for (auto &t : threads) {
            if (t.joinable()) t.join();
        }
        threads.clear();
        // reset io_context to allow potential restart
        io_ctx.reset();
    }
};

EventDispatcher::EventDispatcher(size_t threads)
    : impl_(std::make_unique<Impl>(threads == 0 ? 1 : threads))
{
    impl_->run_threads();
}

EventDispatcher::~EventDispatcher() {
    stop();
}

void EventDispatcher::dispatch(std::function<void()> f) {
    if (!f) return;
    asio::post(impl_->io_ctx, std::move(f));
}

std::shared_ptr<Timer> EventDispatcher::dispatch_delayed(std::chrono::steady_clock::duration dur, std::function<void()> f) {
    if (!f) return nullptr;
    auto timer = std::make_shared<Timer>(impl_->io_ctx, dur, std::move(f));
    return timer;
}

void EventDispatcher::start() {
    impl_->run_threads();
}

void EventDispatcher::stop() {
    impl_->stop_threads();
}

} // namespace CONCURRENCY
