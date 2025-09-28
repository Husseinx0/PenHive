#pragma once
#include <string>
#include <string_view>
#include <libxml/xmlwriter.h>

class VolumeDefinitionBuilder {
    xmlTextWriterPtr writer;
    std::string buffer;

    void finalize();

public:
    VolumeDefinitionBuilder();
    ~VolumeDefinitionBuilder();

    VolumeDefinitionBuilder& setName(std::string_view name);
    VolumeDefinitionBuilder& setFormat(std::string_view format = "qcow2");
    VolumeDefinitionBuilder& setBackingStore(std::string_view backingPath);

    [[nodiscard]] std::string build();
};