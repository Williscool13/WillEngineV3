//
// Created by William on 2026-03-13.
//

#include "material_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWMaterialHeader(Core::Vector<std::byte>& out, const WMaterialHeader& header)
{
    AppendText(out, "wmaterial\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.materialId);
    AppendTextF(out, "name %s\n", header.name);
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WMaterialHeader> ReadWMaterialHeader(const void* data, uint64_t size)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wmaterial") != 0) { return std::nullopt; }

    WMaterialHeader header{};
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != MATERIAL_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.materialId); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WMATERIAL_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
    }
    return std::nullopt;
}

std::optional<WMaterialHeader> ReadWMaterialHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWMaterialHeader(map.data, map.size);
}
} // Engine
