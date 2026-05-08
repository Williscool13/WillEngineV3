//
// Created by William on 2026-05-08.
//

#ifndef WILL_ENGINE_ARENA_STRING_H
#define WILL_ENGINE_ARENA_STRING_H

#include <cassert>
#include <cstring>
#include <string_view>

#include "core/memory/arena.h"

namespace Core
{
/**
 * Arena-backed string. Always null-terminated. Old buffers are stranded in the arena on growth;
 * they are reclaimed when the arena resets.
 */
class ArenaString
{
public:
    ArenaString() = default;

    explicit ArenaString(Arena* arena) : arena_(arena) {}

    ArenaString(Arena* arena, size_t initialCapacity) : arena_(arena) { Grow(initialCapacity); }

    ArenaString(Arena* arena, const char* str)
        : ArenaString(arena, str, str ? strlen(str) : 0)
    {}

    ArenaString(Arena* arena, const char* str, size_t len) : arena_(arena)
    {
        if (len > 0) {
            Grow(len);
            memcpy(data_, str, len);
            data_[len] = '\0';
            size_ = len;
        }
    }

    ArenaString(Arena* arena, std::string_view sv)
        : ArenaString(arena, sv.data(), sv.size())
    {}

    void Append(const char* str, size_t len)
    {
        if (len == 0) { return; }
        const size_t newSize = size_ + len;
        if (newSize > capacity_) { Grow(newSize); }
        memcpy(data_ + size_, str, len);
        size_ = newSize;
        data_[size_] = '\0';
    }

    void Append(const char* str) { Append(str, strlen(str)); }

    void Append(std::string_view sv) { Append(sv.data(), sv.size()); }

    void Append(uint32_t value)
    {
        char tmp[12];
        int written = snprintf(tmp, sizeof(tmp), "%u", value);
        if (written > 0) { Append(tmp, static_cast<size_t>(written)); }
    }

    void Clear()
    {
        size_ = 0;
        if (data_) { data_[0] = '\0'; }
    }

    const char* c_str() const { return data_ ? data_ : ""; }
    size_t Size() const { return size_; }
    bool IsEmpty() const { return size_ == 0; }
    std::string_view View() const { return {c_str(), size_}; }

private:
    void Grow(size_t minCapacity)
    {
        assert(arena_ != nullptr && "ArenaString: no arena");
        size_t newCapacity = capacity_ < 64 ? 64 : capacity_ * 2;
        if (newCapacity < minCapacity) { newCapacity = minCapacity; }
        char* newData = static_cast<char*>(arena_->AllocRaw(newCapacity + 1));
        assert(newData != nullptr && "ArenaString: arena out of memory");
        if (size_ > 0) { memcpy(newData, data_, size_ + 1); }
        else { newData[0] = '\0'; }
        data_ = newData;
        capacity_ = newCapacity;
    }

    Arena* arena_{};
    char* data_{};
    size_t size_{};
    size_t capacity_{};
};
} // Core

#endif //WILL_ENGINE_ARENA_STRING_H
