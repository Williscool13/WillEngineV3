//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_TEXT_PARSE_H
#define WILL_ENGINE_TEXT_PARSE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/containers/inline_string.h"
#include "core/containers/vector.h"

namespace Engine
{
/**
 * Line reader over an in-memory buffer, mirroring istream::getline semantics: consumes up to and including '\n', strips a trailing '\r', null-terminates into buf.
 * Returns false at end of data or when a line exceeds bufSize - 1. offset after a GetLine that hit end_header is the byte offset of the following data.
 */
struct MemLineReader
{
    const char* data{nullptr};
    uint64_t size{0};
    uint64_t offset{0};

    MemLineReader(const void* _data, uint64_t _size) : data(static_cast<const char*>(_data)), size(_size) {}

    bool GetLine(char* buf, size_t bufSize)
    {
        if (offset >= size) { return false; }
        size_t len = 0;
        while (offset < size && data[offset] != '\n') {
            if (len + 1 >= bufSize) { return false; }
            buf[len++] = data[offset++];
        }
        if (offset < size) { ++offset; }
        if (len > 0 && buf[len - 1] == '\r') { --len; }
        buf[len] = '\0';
        return true;
    }
};

inline void AppendText(Core::Vector<std::byte>& out, const char* text)
{
    const auto* p = reinterpret_cast<const std::byte*>(text);
    out.Append(p, p + strlen(text));
}

template<typename... Args>
void AppendTextF(Core::Vector<std::byte>& out, const char* fmt, Args&&... args)
{
    AppendText(out, Core::InlineString<512>::Format(fmt, static_cast<Args&&>(args)...).c_str());
}
} // Engine

#endif //WILL_ENGINE_TEXT_PARSE_H
