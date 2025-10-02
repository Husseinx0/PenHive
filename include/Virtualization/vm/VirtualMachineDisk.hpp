#pragma once
#include <string>

struct VirtualMachineDisk {
    std::string path;
    std::string format; // qcow2, raw, ...
    unsigned long sizeKB;
    bool readonlyFlag{false};
};
