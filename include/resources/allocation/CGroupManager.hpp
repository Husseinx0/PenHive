#include <cerrno>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include "c"
class CGroupManager {
private:
  std::string cgroupPath;

public:
  CGroupManager(const std::string& name) {
    cgroupPath = "/sys/fs/cgroup/" + name;
    createCGroup();
  }

  void createCGroup() {
    if (mkdir(cgroupPath.c_str(), 0755) != 0 && errno != EEXIST) {
      throw std::system_error(errno, std::system_category(), "Failed to create cgroup");
    }
  }

  void setCPULimit(unsigned long quota_us, unsigned long period_us = 100000) {
    writeValue("cpu.max", std::to_string(quota_us) + " " + std::to_string(period_us));
  }

  void setMemoryLimit(const std::string& limit) { writeValue("memory.max", limit); }

  void addProcess(pid_t pid) { writeValue("cgroup.procs", std::to_string(pid)); }

private:
  void writeValue(const std::string& file, const std::string& value) {
    std::ofstream f(cgroupPath + "/" + file);
    if (!f) {
      throw std::runtime_error("Cannot write to cgroup file: " + file);
    }
    f << value;
  }
};
