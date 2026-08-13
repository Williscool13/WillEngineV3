//
// Created by William on 2026-03-21.
//

#include "prefab_format.h"

#include <charconv>
#include <cstring>

#include <json/nlohmann/json.hpp>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWPrefabHeader(Core::Vector<std::byte>& out, const WPrefabHeader& header)
{
    AppendText(out, "wprefab\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.prefabId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "component_count %u\n", header.componentCount);
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WPrefabHeader> ReadWPrefabHeader(const void* data, uint64_t size)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wprefab") != 0) { return std::nullopt; }

    WPrefabHeader header{};
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != PREFAB_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.prefabId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.contentVersion); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WPREFAB_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "component_count ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.componentCount); }
    }
    return std::nullopt;
}

std::optional<WPrefabHeader> ReadWPrefabHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWPrefabHeader(map.data, map.size);
}

std::optional<WPrefabData> ReadWPrefab(const char* path)
{
    Platform::ScopedFileMapping map{Core::Path(path)};
    if (!map.data) {
        return std::nullopt;
    }

    auto header = ReadWPrefabHeader(map.data, map.size);
    if (!header) {
        return std::nullopt;
    }

    auto json = nlohmann::json::parse(map.data + header->dataOffset, map.data + map.size, nullptr, false);
    if (json.is_discarded()) {
        return std::nullopt;
    }

    return WPrefabData{.header = *header, .componentJson = std::move(json)};
}
} // Engine
