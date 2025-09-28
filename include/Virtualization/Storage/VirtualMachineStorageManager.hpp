#pragma once
#include <string>
#include <vector>
#include <memory>
#include <string_view>
#include "System/Result.hpp"
#include "System/VmException.hpp"

class StorageOrchestrator {
    std::shared_ptr<class HypervisorConnector> connector;

public:
    explicit StorageOrchestrator(std::shared_ptr<HypervisorConnector> conn);

    [[nodiscard]] Result<std::vector<std::string>> listBaseVolumes() const;
    [[nodiscard]] Result<std::string> createLinkedClone(std::string_view baseName, std::string_view cloneBaseName);
    [[nodiscard]] Result<void> deleteVolume(std::string_view volumeName);
    [[nodiscard]] Result<std::string> getVolumePath(std::string_view volumeName) const;
};