//
// Created by William on 2026-03-13.
//

#include "scene_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
bool WriteWSceneHeader(Core::Vector<std::byte>& out, const WSceneHeader& header)
{
    AppendText(out, "wscene\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "id %llu\n", header.sceneId);
    AppendTextF(out, "content_version %llu\n", header.contentVersion);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "entity_count %u\n", header.entityCount);
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WSceneHeader> ReadWSceneHeader(const void* data, uint64_t size)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wscene") != 0) { return std::nullopt; }

    WSceneHeader header{};
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != SCENE_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "id ", 3) == 0) { std::from_chars(line + 3, line + LINE_BUF, header.sceneId); }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.contentVersion); }
        else if (strncmp(line, "name ", 5) == 0) {
            const char* name = line + 5;
            const size_t copyLen = std::min(strlen(name), WSCENE_NAME_LENGTH - 1);
            memcpy(header.name, name, copyLen);
            header.name[copyLen] = '\0';
        }
        else if (strncmp(line, "entity_count ", 13) == 0) { std::from_chars(line + 13, line + LINE_BUF, header.entityCount); }
    }
    return std::nullopt;
}

std::optional<WSceneHeader> ReadWSceneHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWSceneHeader(map.data, map.size);
}
} // Engine
