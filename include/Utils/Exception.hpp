#pragma once
#include <stdexcept>
#include <string>

class LibvirtException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class VMException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};