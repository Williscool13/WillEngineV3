//
// Created by William on 2026-05-14.
//

#include "text_material_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWTextMaterialHeader(Core::Vector<std::byte>& out, const WTextMaterialHeader& header)
{
    AppendText(out, "wstextmat\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.textMaterialId);
    AppendTextF(out, "name %s\n", header.name);
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const void* data, uint64_t size)
{
    constexpr size_t LINE_BUF = 512;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wstextmat") != 0) { return std::nullopt; }

    WTextMaterialHeader header{};
    while (in.GetLine(line, LINE_BUF)) {
        const char* end = line + strlen(line);
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, end, major);
            if (major != TEXT_MATERIAL_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, end, header.textMaterialId); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WTEXT_MATERIAL_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
    }
    return std::nullopt;
}

std::optional<WTextMaterialHeader> ReadWTextMaterialHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWTextMaterialHeader(map.data, map.size);
}
} // Engine
