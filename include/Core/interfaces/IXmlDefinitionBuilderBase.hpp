#pragma once

#include <pugixml.hpp>
#include <string>
#include <string_view>
#include <concepts>

/**
 * @brief Abstract base class for building XML documents
 * 
 * Provides common interface and functionality for XML document construction.
 * Derived classes must implement the specific document structure.
 */
class IXmlBuilderBase {
protected:
    pugi::xml_document doc;     ///< Underlying XML document
    std::string buffer;         ///< Temporary data storage

    /**
     * @brief Constructs the XML document structure
     * 
     * Pure virtual function that derived classes must implement
     * to define the specific XML structure.
     */
    virtual void buildDocument() = 0;

public:
    IXmlBuilderBase() = default;
    
    // Non-copyable
    IXmlBuilderBase(const IXmlBuilderBase&) = delete;
    IXmlBuilderBase& operator=(const IXmlBuilderBase&) = delete;
    
    // Movable
    IXmlBuilderBase(IXmlBuilderBase&&) noexcept = default;
    IXmlBuilderBase& operator=(IXmlBuilderBase&&) noexcept = default;

    /**
     * @brief Builds and returns the formatted XML document
     * 
     * @return std::string Formatted XML content
     * 
     * @note Uses return value optimization (RVO) for efficiency
     */
    [[nodiscard]] std::string build() {
        buildDocument(); // Delegate to derived implementation

        // Use string writer to capture the XML output
        struct xml_string_writer : pugi::xml_writer {
            std::string result;
            void write(const void* data, size_t size) override {
                result.append(static_cast<const char*>(data), size);
            }
        };

        xml_string_writer writer;
        doc.save(writer, "  ", pugi::format_default | pugi::format_indent);
        return writer.result;
    }

    /**
     * @brief Resets the builder to initial state
     * 
     * Clears the document and buffer, allowing the builder
     * to be reused for constructing new documents.
     */
    void reset() noexcept {
        doc.reset();
        buffer.clear();
        
        // Shrink buffer if capacity becomes excessive
        if (buffer.capacity() > 1024) {
            buffer = std::string{}; // Modern shrink-to-fit
        }
    }

    /**
     * @brief Provides access to the underlying XML document
     * 
     * @return const pugi::xml_document& Reference to the internal document
     * 
     * @warning Modifying the returned document may break builder state
     */
    [[nodiscard]] const pugi::xml_document& getDocument() const noexcept {
        return doc;
    }

    virtual ~IXmlBuilderBase() = default;
};