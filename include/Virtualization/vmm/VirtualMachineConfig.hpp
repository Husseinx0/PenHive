#ifndef VMCONFIG_H
#define VMCONFIG_H

#include <string>
#include <vector>
#include <map>
#include <memory>

struct DiskConfig {
    std::string type; // file, block, network
    std::string device; // disk, cdrom, floppy
    std::string source; // path to image
    std::string target; // device name (e.g., vda)
    std::string driver; // driver type
    unsigned long size; // in KB
    bool readOnly;
};

struct NetworkConfig {
    std::string type; // bridge, network, user
    std::string source; // network name or bridge name
    std::string model; // network card model
    std::string macAddress;
};

struct GraphicsConfig {
    std::string type; // vnc, spice, sdl
    std::string listenAddress;
    int port;
    bool autoport;
};

struct VmConfig {
    std::string name;
    std::string uuid;
    std::string title;
    std::string description;
    std::string osType;
    std::string arch;
    
    // الموارد
    unsigned long memory; // in KB
    unsigned long currentMemory; // in KB
    unsigned int vcpus;
    unsigned int maxVcpus;
    
    // التخزين والشبكات
    std::vector<DiskConfig> disks;
    std::vector<NetworkConfig> networks;
    
    // إعدادات أخرى
    GraphicsConfig graphics;
    std::map<std::string, std::string> metadata;
    
    // التوافقية
    std::string emulator;
    
    // التهيئة من/إلى XML
    static VmConfig fromXML(const std::string& xml);
    std::string toXML() const;
    
    // التحقق من الصحة
    bool validate() const;
};

#endif // VMCONFIC_H