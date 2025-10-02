#pragma once 
#include <nlohmann/json.hpp>
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <source_location>
#include <string_view>
#include <utility>

namespace nlohmann {
    using json = nlohmann::json;
}

/**
 * Modern JSON template class using nlohmann::json with C++23 features.
 * Interface only — implementation lives in src/Template/Template.cpp
 */
class Template {
public:
    Template() = default;

    [[nodiscard]] std::expected<void, std::string> parse(
        std::string_view json_str,
        std::source_location loc = std::source_location::current()
    ) noexcept;

    [[nodiscard]] std::string to_string() const;

    void set(std::string key, nlohmann::json value);
    void add(std::string key, nlohmann::json value);
    [[nodiscard]] std::optional<nlohmann::json> get(std::string_view key) const;
    [[nodiscard]] std::vector<nlohmann::json> get_all(std::string_view key) const;
    [[nodiscard]] bool remove(std::string_view key);
    [[nodiscard]] bool empty() const;
    void merge(const Template& other);
    void encrypt(const std::string& key);
    void decrypt(const std::string& key);

private:
    nlohmann::json data;
    [[nodiscard]] bool is_sensitive_field(std::string_view key) const;
    [[nodiscard]] std::string encrypt_string(std::string_view plain, std::string_view key) const;
    [[nodiscard]] std::string decrypt_string(std::string_view encrypted, std::string_view key) const;
};
    /**
     * @brief Convert to JSON string with pretty printing (4-space indentation)
     * @return Formatted JSON string
     */
    #include <map>
    [[nodiscard]] std::string to_string() const{
       
        
    }

    /**
     * @brief Set a key-value pair (replaces existing value)
     * @param key Attribute name
     * @param value Attribute value (nlohmann::json type)
     */
    void set(std::string key, nlohmann::json value);

    /**
     * @brief Add value to existing key (appends to array or creates new array)
     * @param key Attribute name
     * @param value Value to add
     */
    void add(std::string key, nlohmann::json value);

    /**
     * @brief Get first value for key (handles arrays)
     * @param key Attribute name
     * @return Optional containing value or nullopt if not found
     */
    [[nodiscard]] std::optional<nlohmann::json> get(std::string_view key) const;

    /**
     * @brief Get all values for key as vector (single values become single-element vectors)
     * @param key Attribute name
     * @return Vector of all values
     */
    [[nodiscard]] std::vector<nlohmann::json> get_all(std::string_view key) const;

    /**
     * @brief Remove key from template
     * @param key Attribute name
     * @return true if key existed and was removed
     */
    [[nodiscard]] bool remove(std::string_view key);

    /**
     * @brief Check if template is empty
     * @return true if no attributes exist
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Merge another template (overwrites existing keys)
     * @param other Template to merge from
     */
    void merge(const Template& other);

    /**
     * @brief Encrypt sensitive fields using provided key
     * @param key Encryption key
     */
    void encrypt(const std::string& key);

    /**
     * @brief Decrypt sensitive fields using provided key
     * @param key Decryption key
     */
    void decrypt(const std::string& key);

private:
    nlohmann::json data;

    /**
     * @brief Check if field name is sensitive (case-insensitive match)
     * @param key Field name to check
     * @return true if sensitive
     */
    [[nodiscard]] bool is_sensitive_field(std::string_view key) const;

    /**
     * @brief Encrypt string using provided key
     * @param plain Plain text string
     * @param key Encryption key
     * @return Encrypted string
     */
    [[nodiscard]] std::string encrypt_string(std::string_view plain, std::string_view key) const;

    /**
     * @brief Decrypt string using provided key
     * @param encrypted Encrypted string
     * @param key Decryption key
     * @return Decrypted string
     */
    [[nodiscard]] std::string decrypt_string(std::string_view encrypted, std::string_view key) const;
};

