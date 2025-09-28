#pragma once
#include <stdexcept>
#include <string>

class VmException : public std::runtime_error {
public:
    explicit VmException(const std::string& msg) : std::runtime_error("[VmException] " + msg) {}
};

class LibvirtException : public VmException {
public:
    explicit LibvirtException(const std::string& msg) : VmException("[Libvirt] " + msg) {}
};

class StorageException : public VmException {
public:
    explicit StorageException(const std::string& msg) : VmException("[Storage] " + msg) {}
};