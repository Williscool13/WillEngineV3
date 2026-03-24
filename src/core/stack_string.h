//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STACK_STRING_H
#define WILL_ENGINE_STACK_STRING_H
#include <cstring>

template<size_t N = 16>
struct StackString
{
    char buf[N] = {};
    size_t len = 0;

    StackString() = default;

    explicit StackString(const char* str)
    {
        len = strlen(str);
        if (len >= N) len = N - 1;
        memcpy(buf, str, len);
        buf[len] = '\0';
    }

    [[nodiscard]] const char* c_str() const { return buf; }
    [[nodiscard]] size_t size() const { return len; }

    bool operator==(const StackString& other) const { return len == other.len && memcmp(buf, other.buf, len) == 0; }
    bool operator==(const char* str) const { return strcmp(buf, str) == 0; }
    auto operator<=>(const StackString& other) const { return strcmp(buf, other.buf) <=> 0; }
};

using ShortString = StackString<16>;

#endif //WILL_ENGINE_STACK_STRING_H
