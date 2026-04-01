//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STACK_STRING_H
#define WILL_ENGINE_STACK_STRING_H
#include <cstring>

namespace Core
{
template<size_t N = 16>
struct InlineString
{
    char buf[N] = {};
    size_t len = 0;

    InlineString() = default;

    explicit InlineString(const char* str)
    {
        len = strlen(str);
        if (len >= N) len = N - 1;
        memcpy(buf, str, len);
        buf[len] = '\0';
    }

    [[nodiscard]] const char* c_str() const { return buf; }
    [[nodiscard]] size_t size() const { return len; }

    bool operator==(const InlineString& other) const { return len == other.len && memcmp(buf, other.buf, len) == 0; }
    bool operator==(const char* str) const { return strcmp(buf, str) == 0; }
    auto operator<=>(const InlineString& other) const { return strcmp(buf, other.buf) <=> 0; }
};

using ShortString = InlineString<>;
} // Core


#endif //WILL_ENGINE_STACK_STRING_H
