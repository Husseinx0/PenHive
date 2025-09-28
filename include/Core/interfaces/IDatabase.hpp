#pragma once

#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/iterator.h>
#include <memory>
#include <string>
#include <string_view>
#include <expected> // C++23
#include <concepts>
#include "Utils/Result.hpp"
/**
 * @brief Abstract interface for RocksDB operations using modern C++23.
 */
class IRocksDB {
public:
    virtual ~IRocksDB() noexcept = default;

    // Open the database
    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Open(const rocksdb::Options& options, 
        std::string_view name
    ) noexcept = 0;

    // Put a key-value pair
    [[nodiscard]] virtual Result<void, rocksdb::Status> 
    Put(){
        
    }
    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Put(const rocksdb::WriteOptions& options,
        std::string_view key,
        std::string_view value
    ) noexcept = 0;

    // Get value by key
    [[nodiscard]] virtual std::expected<std::string, rocksdb::Status>
    Get(const rocksdb::ReadOptions& options, std::string_view key) const noexcept = 0;

    // Delete a key
    [[nodiscard]] virtual std::expected<void, rocksdb::Status>
    Delete(const rocksdb::WriteOptions& options, std::string_view key) noexcept = 0;

    // Create a new iterator
    [[nodiscard]] virtual std::unique_ptr<rocksdb::Iterator>
    NewIterator(const rocksdb::ReadOptions& options) noexcept = 0;

    // Close the database safely
    [[nodiscard]] virtual bool Close() noexcept = 0;
    virtual Result<int> allocate();

    template<typename T>
    [[nodiscard]] std::unique_ptr<T> get(int oid) {
        if ( oid < 0 )
            return nullptr;
        
        

    }
    //وظائف ساعمل عليها لاحقا 
    /*
        virtual bool batchWrite(const std::vector<std::pair<std::string, std::string>>& operations) = 0;
    virtual bool merge(const std::string& key, const std::string& value) = 0; // مهم لـ CRDTs!

    // التكرار (Iteration)
    virtual std::vector<std::pair<std::string, std::string>> scan(const std::string& prefix = "") = 0;

    // إدارة قاعدة البيانات
    virtual bool open() = 0;
    virtual bool close() = 0;
    virtual bool isOpen() const = 0;

    */
};