//
// Created by William on 2026-09-09.
//

#include "play_format.h"

#include <charconv>
#include <cstring>

#include "engine/serialization/text_parse.h"
#include "platform/file_utils.h"

namespace Engine
{
static void CopyHeaderString(char* dst, const char* src)
{
    const size_t copyLen = std::min(strlen(src), WPLAY_NAME_LENGTH - 1);
    memcpy(dst, src, copyLen);
    dst[copyLen] = '\0';
}

bool WriteWPlayHeader(Core::Vector<std::byte>& out, const WPlayHeader& header)
{
    AppendText(out, "wplay\n");
    AppendTextF(out, "version %u %u\n", header.major, header.minor);
    AppendTextF(out, "name %s\n", header.name);
    AppendTextF(out, "scene %s\n", header.scene);
    AppendTextF(out, "content_version %llu\n", static_cast<unsigned long long>(header.contentVersion));
    AppendTextF(out, "event_count %u\n", header.eventCount);
    AppendText(out, "end_header\n");
    return true;
}

std::optional<WPlayHeader> ReadWPlayHeader(const void* data, uint64_t size)
{
    constexpr size_t LINE_BUF = 256;
    char line[LINE_BUF];
    MemLineReader in(data, size);

    if (!in.GetLine(line, LINE_BUF)) { return std::nullopt; }
    if (strcmp(line, "wplay") != 0) { return std::nullopt; }

    WPlayHeader header{};
    while (in.GetLine(line, LINE_BUF)) {
        if (strcmp(line, "end_header") == 0) {
            header.dataOffset = in.offset;
            return header;
        }
        if (strncmp(line, "version ", 8) == 0) {
            uint32_t major = 0;
            std::from_chars(line + 8, line + LINE_BUF, major);
            if (major != PLAY_MAJOR_VERSION) { return std::nullopt; }
        }
        else if (strncmp(line, "content_version ", 16) == 0) { std::from_chars(line + 16, line + LINE_BUF, header.contentVersion); }
        else if (strncmp(line, "name ", 5) == 0) { CopyHeaderString(header.name, line + 5); }
        else if (strncmp(line, "scene ", 6) == 0) { CopyHeaderString(header.scene, line + 6); }
        else if (strncmp(line, "event_count ", 12) == 0) { std::from_chars(line + 12, line + LINE_BUF, header.eventCount); }
    }
    return std::nullopt;
}

std::optional<WPlayHeader> ReadWPlayHeader(const Core::Path& path)
{
    Platform::ScopedFileMapping map(path);
    if (!map.data) { return std::nullopt; }
    return ReadWPlayHeader(map.data, map.size);
}
} // Engine
