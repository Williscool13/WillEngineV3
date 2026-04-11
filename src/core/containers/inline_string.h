//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STACK_STRING_H
#define WILL_ENGINE_STACK_STRING_H
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Core
{
template<size_t N = 64>
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
    explicit InlineString(std::string_view str)
    {
        len = str.size();
        if (len >= N) len = N - 1;
        memcpy(buf, str.data(), len);
        buf[len] = '\0';
    }

    template<typename... Args>
    [[nodiscard]] static InlineString Format(const char* fmt, Args&&... args)
    {
        InlineString result;
        int written = snprintf(result.buf, N, fmt, static_cast<Args&&>(args)...);
        result.len = (written > 0 && static_cast<size_t>(written) < N) ? static_cast<size_t>(written) : N - 1;
        return result;
    }

    void Clear() { buf[0] = '\0'; len = 0; }

    [[nodiscard]] std::string_view View() const { return {buf, len}; }

    [[nodiscard]] const char* c_str() const { return buf; }
    [[nodiscard]] size_t Size() const { return len; }
    [[nodiscard]] bool IsEmpty() const { return len == 0; }

    bool Append(const char* str)
    {
        return Append(str, strlen(str));
    }

    bool Append(std::string_view str)
    {
        return Append(str.data(), str.size());
    }

    template<size_t M>
    bool Append(const InlineString<M>& other)
    {
        return Append(other.buf, other.len);
    }

    bool Append(int32_t value)
    {
        char tmp[24];
        int written = snprintf(tmp, sizeof(tmp), "%d", value);
        if (written <= 0) { return false; }
        return Append(tmp, static_cast<size_t>(written));
    }

    bool Append(uint32_t value)
    {
        char tmp[24];
        int written = snprintf(tmp, sizeof(tmp), "%u", value);
        if (written <= 0) { return false; }
        return Append(tmp, static_cast<size_t>(written));
    }

    bool Append(int64_t value)
    {
        char tmp[24];
        int written = snprintf(tmp, sizeof(tmp), "%lld", value);
        if (written <= 0) { return false; }
        return Append(tmp, static_cast<size_t>(written));
    }

    bool Append(uint64_t value)
    {
        char tmp[24];
        int written = snprintf(tmp, sizeof(tmp), "%llu", value);
        if (written <= 0) { return false; }
        return Append(tmp, static_cast<size_t>(written));
    }

    bool Append(const char* str, size_t strLen)
    {
        if (len + strLen >= N) {
            return false;
        }
        memcpy(buf + len, str, strLen);
        len += strLen;
        buf[len] = '\0';
        return true;
    }

    void Replace(char from, char to)
    {
        for (size_t i = 0; i < len; ++i) {
            if (buf[i] == from) { buf[i] = to; }
        }
    }

    void ToUpper()
    {
        for (size_t i = 0; i < len; ++i) {
            buf[i] = static_cast<char>(toupper(static_cast<unsigned char>(buf[i])));
        }
    }

    void ToLower()
    {
        for (size_t i = 0; i < len; ++i) {
            buf[i] = static_cast<char>(tolower(static_cast<unsigned char>(buf[i])));
        }
    }

    bool operator==(const InlineString& other) const { return len == other.len && memcmp(buf, other.buf, len) == 0; }
    bool operator==(const char* str) const { return strcmp(buf, str) == 0; }
    auto operator<=>(const InlineString& other) const { return strcmp(buf, other.buf) <=> 0; }
};

using ShortString = InlineString<>;
} // Core


#endif //WILL_ENGINE_STACK_STRING_H
