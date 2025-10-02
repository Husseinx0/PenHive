#pragma once

#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/iterator.h>
#include <memory>
#include <string>
#include <string_view>
#include <expected> // C++23
#include "Utils/Result.hpp"

/**
 * @brief Abstract interface for RocksDB operations using modern C++23.
 */
class IRocksDB {
public:
    virtual ~IRocksDB() noexcept = default;

    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Open(const rocksdb::Options& options, std::string_view name) noexcept = 0;

    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Put(const rocksdb::WriteOptions& options, std::string_view key, std::string_view value) noexcept = 0;

    [[nodiscard]] virtual std::expected<std::string, rocksdb::Status>
    Get(const rocksdb::ReadOptions& options, std::string_view key) const noexcept = 0;

    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Delete(const rocksdb::WriteOptions& options, std::string_view key) noexcept = 0;

    [[nodiscard]] virtual std::unique_ptr<rocksdb::Iterator>
    NewIterator(const rocksdb::ReadOptions& options) noexcept = 0;

    [[nodiscard]] virtual bool Close() noexcept = 0;
};