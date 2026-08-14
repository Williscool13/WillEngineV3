//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_INLINE_PATH_H
#define WILL_ENGINE_INLINE_PATH_H

#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Core
{
/**
 * Fixed-capacity path with inline storage. No heap allocation.
 * Always null-terminated. Separators are normalized to '/'.
 *
 * The std::filesystem::path constructor exists for scan-time conversion only;
 * all other operations are allocation-free.
 */
template<size_t N>
class InlinePath
{
    static_assert(N > 1, "InlinePath: N must be > 1");

public:
    InlinePath() = default;

    explicit InlinePath(const char* str)
    {
        if (!str) { return; }
        Assign(str, strlen(str));
    }

    explicit InlinePath(std::string_view sv)
    {
        Assign(sv.data(), sv.size());
    }

    // Returns a new path with the component appended, separated by '/'.
    InlinePath operator/(const std::string_view component) const
    {
        InlinePath result = *this;
        if (result.len_ > 0 && result.buf_[result.len_ - 1] != '/') {
            result.Append("/", 1);
        }
        result.Append(component.data(), component.size());
        return result;
    }

    // Everything before the last component. e.g. "/path/to" from "/path/to/foo.wtexture". Empty if no separator.
    [[nodiscard]] InlinePath Parent() const
    {
        size_t sep = len_;
        while (sep > 0 && buf_[sep - 1] != '/') { --sep; }
        if (sep > 0) { --sep; } // skip the '/' itself
        return InlinePath(std::string_view{buf_, sep});
    }

    // Last path component. e.g. "foo.wtexture" from "/path/to/foo.wtexture".
    [[nodiscard]] std::string_view Filename() const
    {
        size_t sep = len_;
        while (sep > 0 && buf_[sep - 1] != '/') { --sep; }
        return {buf_ + sep, len_ - sep};
    }

    // e.g. ".wtexture" including the dot, or empty if no extension in filename.
    [[nodiscard]] std::string_view Extension() const
    {
        const std::string_view fn = Filename();
        const size_t fnStart = static_cast<size_t>(fn.data() - buf_);
        for (size_t i = len_; i > fnStart + 1; --i) {
            if (buf_[i - 1] == '.') { return {buf_ + i - 1, len_ - (i - 1)}; }
        }
        return {};
    }

    // Filename without extension. e.g. "foo" from "/path/foo.wtexture".
    [[nodiscard]] std::string_view Stem() const
    {
        const std::string_view fn = Filename();
        const size_t fnStart = static_cast<size_t>(fn.data() - buf_);
        for (size_t i = len_; i > fnStart + 1; --i) {
            if (buf_[i - 1] == '.') { return {buf_ + fnStart, (i - 1) - fnStart}; }
        }
        return fn;
    }

    /** True when any segment starts with '.' (dot-dir convention: tool-generated intermediates, never authored content). */
    [[nodiscard]] bool HasHiddenSegment() const
    {
        for (size_t i = 0; i < len_; ++i) {
            if (buf_[i] == '.' && (i == 0 || buf_[i - 1] == '/')) { return true; }
        }
        return false;
    }

    [[nodiscard]] bool Exists() const
    {
#ifdef _WIN32
        return _access(buf_, 0) == 0;
#else
        return access(buf_, F_OK) == 0;
#endif
    }

    [[nodiscard]] const char* c_str() const { return buf_; }
    [[nodiscard]] std::string_view View() const { return {buf_, len_}; }
    [[nodiscard]] size_t Size() const { return len_; }
    [[nodiscard]] bool IsEmpty() const { return len_ == 0; }

    bool operator==(const InlinePath& other) const
    {
        return len_ == other.len_ && memcmp(buf_, other.buf_, len_) == 0;
    }

    bool operator==(const char* str) const
    {
        if (!str) { return len_ == 0; }
        return strcmp(buf_, str) == 0;
    }

    bool operator!=(const InlinePath& other) const { return !(*this == other); }
    bool operator!=(const char* str) const { return !(*this == str); }

private:
    void Assign(const char* str, size_t len)
    {
        len_ = len < N - 1 ? len : N - 1;
        memcpy(buf_, str, len_);
        buf_[len_] = '\0';
    }

    void Append(const char* str, size_t len)
    {
        const size_t available = (N - 1) - len_;
        const size_t copy = len < available ? len : available;
        memcpy(buf_ + len_, str, copy);
        len_ += copy;
        buf_[len_] = '\0';
    }

    void Normalize()
    {
        for (size_t i = 0; i < len_; ++i) {
            if (buf_[i] == '\\') { buf_[i] = '/'; }
        }
    }

    char buf_[N]{};
    size_t len_{};
};
using Path = InlinePath<256>;
} // namespace Core

#endif // WILL_ENGINE_INLINE_PATH_H
