#pragma once
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <string>
#include <chrono>
#include <thread>
#include <iostream>
namespace CONCURRENCY {
namespace asio = boost::asio;
using namespace std::chrono_literals;
class Timer
{
private:
    /* data */
public:
    Timer (/* args */);
    ~Timer ();
};

class EventDispatcher
{

public:
    EventDispatcher (/* args */);
    ~EventDispatcher ();
    void dispatch (std::function<void ()> f) {
        if (!f)
            return;
        asio::post(io_ctx_,f);
    }
private:
    asio::io_context io_ctx_;
};
} // namespace CONCURRENCY
