//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STACK_STRING_H
#define WILL_ENGINE_STACK_STRING_H
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

    bool operator==(const InlineString& other) const { return len == other.len && memcmp(buf, other.buf, len) == 0; }
    bool operator==(const char* str) const { return strcmp(buf, str) == 0; }
    auto operator<=>(const InlineString& other) const { return strcmp(buf, other.buf) <=> 0; }
};

using ShortString = InlineString<>;
} // Core


#endif //WILL_ENGINE_STACK_STRING_H
