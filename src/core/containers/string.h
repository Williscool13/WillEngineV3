//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_STRING_H
#define WILL_ENGINE_STRING_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Heap-backed string backed by a TlsfAllocator. Always null-terminated.
 * For stack/fixed-size strings use StackString<N>.
 */
class String
{
public:
    String() = default;

    String(TlsfAllocator* alloc, AllocTag tag, const char* str)
        : String(alloc, tag, str, str ? strlen(str) : 0)
    {}

    String(TlsfAllocator* alloc, AllocTag tag, const char* str, size_t len)
        : alloc_(alloc), tag_(tag)
    {
        if (len > 0) {
            Grow(len);
            memcpy(data_, str, len);
            data_[len] = '\0';
            size_ = len;
        }
    }

    String(TlsfAllocator* alloc, AllocTag tag, std::string_view sv)
        : String(alloc, tag, sv.data(), sv.size())
    {}

    ~String() { Free(); }

    String(const String& other)
        : alloc_(other.alloc_), tag_(other.tag_)
    {
        if (other.size_ > 0) {
            Grow(other.size_);
            memcpy(data_, other.data_, other.size_ + 1);
            size_ = other.size_;
        }
    }

    String& operator=(const String& other)
    {
        if (this == &other) { return *this; }
        Free();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        if (other.size_ > 0) {
            Grow(other.size_);
            memcpy(data_, other.data_, other.size_ + 1);
            size_ = other.size_;
        }
        return *this;
    }

    String(String&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    String& operator=(String&& other) noexcept
    {
        if (this == &other) { return *this; }
        Free();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    void Append(const char* str, size_t len)
    {
        if (len == 0) { return; }
        const size_t newSize = size_ + len;
        if (newSize > capacity_) { Grow(newSize); }
        memcpy(data_ + size_, str, len);
        size_ = newSize;
        data_[size_] = '\0';
    }

    void Append(std::string_view sv) { Append(sv.data(), sv.size()); }

    void Clear()
    {
        size_ = 0;
        if (data_) { data_[0] = '\0'; }
    }

    const char* c_str() const { return data_ ? data_ : ""; }
    size_t Size() const { return size_; }
    bool IsEmpty() const { return size_ == 0; }

    char operator[](size_t i) const
    {
        assert(i < size_);
        return data_[i];
    }

    char& operator[](size_t i)
    {
        assert(i < size_);
        return data_[i];
    }

    bool operator==(const String& other) const
    {
        return size_ == other.size_ && memcmp(data_, other.data_, size_) == 0;
    }

    bool operator==(const char* other) const
    {
        if (!other) { return size_ == 0; }
        return strcmp(c_str(), other) == 0;
    }

    bool operator!=(const String& other) const { return !(*this == other); }
    bool operator!=(const char* other) const { return !(*this == other); }

    std::string_view View() const { return {c_str(), size_}; }

private:
    void Grow(size_t minCapacity)
    {
        assert(alloc_ != nullptr && "String: no allocator");
        size_t newCapacity = capacity_ < 16 ? 16 : capacity_ * 2;
        if (newCapacity < minCapacity) { newCapacity = minCapacity; }
        // +1 for null terminator
        void* raw;
        if (data_) {
            raw = alloc_->Realloc(data_, newCapacity + 1);
        }
        else {
            raw = alloc_->Alloc(newCapacity + 1, tag_);
        }
        assert(raw != nullptr && "String: allocation failed");
        data_ = static_cast<char*>(raw);
        capacity_ = newCapacity;
    }

    void Free()
    {
        if (data_) {
            alloc_->Free(data_);
            data_ = nullptr;
            size_ = 0;
            capacity_ = 0;
        }
    }

    TlsfAllocator* alloc_{};
    AllocTag tag_{AllocTag::Unknown};
    char* data_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_STRING_H
