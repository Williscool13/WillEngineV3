//
// Created by William on 2026-04-02.
//

#ifndef WILL_ENGINE_FUNCTION_H
#define WILL_ENGINE_FUNCTION_H

#include <cassert>
#include <type_traits>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
template<typename Sig>
class Function;

template<typename R, typename... Args>
class Function<R(Args...)>
{
public:
    Function() = default;

    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Function>>>
    Function(F&& f, TlsfAllocator* alloc, AllocTag tag = AllocTag::Unknown)
        : alloc_(alloc), tag_(tag), size_(sizeof(std::decay_t<F>))
    {
        using FD = std::decay_t<F>;
        storage_ = alloc_->Alloc(size_, tag_);
        assert(storage_ != nullptr && "Function: allocation failed");
        new(storage_) FD(std::forward<F>(f));
        invoker_    = [](const void* s, Args... a) -> R { return (*static_cast<const FD*>(s))(std::forward<Args>(a)...); };
        destructor_ = [](void* s) { static_cast<FD*>(s)->~FD(); };
        copier_     = [](const void* src, void* dst) { new(dst) FD(*static_cast<const FD*>(src)); };
        mover_      = [](void* src, void* dst) { new(dst) FD(std::move(*static_cast<FD*>(src))); };
    }

    ~Function() { Reset(); }

    Function(const Function& other)
        : alloc_(other.alloc_), tag_(other.tag_), size_(other.size_),
          invoker_(other.invoker_), destructor_(other.destructor_), copier_(other.copier_), mover_(other.mover_)
    {
        if (other.storage_) {
            storage_ = alloc_->Alloc(size_, tag_);
            assert(storage_ != nullptr && "Function: allocation failed");
            copier_(other.storage_, storage_);
        }
    }

    Function& operator=(const Function& other)
    {
        if (this != &other) {
            Reset();
            alloc_      = other.alloc_;
            tag_        = other.tag_;
            size_       = other.size_;
            invoker_    = other.invoker_;
            destructor_ = other.destructor_;
            copier_     = other.copier_;
            mover_      = other.mover_;
            if (other.storage_) {
                storage_ = alloc_->Alloc(size_, tag_);
                assert(storage_ != nullptr && "Function: allocation failed");
                copier_(other.storage_, storage_);
            }
        }
        return *this;
    }

    Function(Function&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_), storage_(other.storage_), size_(other.size_),
          invoker_(other.invoker_), destructor_(other.destructor_), copier_(other.copier_), mover_(other.mover_)
    {
        other.storage_    = nullptr;
        other.invoker_    = nullptr;
        other.destructor_ = nullptr;
        other.copier_     = nullptr;
        other.mover_      = nullptr;
    }

    Function& operator=(Function&& other) noexcept
    {
        if (this != &other) {
            Reset();
            alloc_      = other.alloc_;
            tag_        = other.tag_;
            storage_    = other.storage_;
            size_       = other.size_;
            invoker_    = other.invoker_;
            destructor_ = other.destructor_;
            copier_     = other.copier_;
            mover_      = other.mover_;
            other.storage_    = nullptr;
            other.invoker_    = nullptr;
            other.destructor_ = nullptr;
            other.copier_     = nullptr;
            other.mover_      = nullptr;
        }
        return *this;
    }

    R operator()(Args... args) const
    {
        assert(invoker_ && "Function called while empty");
        return invoker_(storage_, std::forward<Args>(args)...);
    }

    explicit operator bool() const { return invoker_ != nullptr; }

private:
    void Reset()
    {
        if (storage_) {
            if (destructor_) { destructor_(storage_); }
            alloc_->Free(storage_);
            storage_ = nullptr;
        }
        invoker_    = nullptr;
        destructor_ = nullptr;
        copier_     = nullptr;
        mover_      = nullptr;
    }

    TlsfAllocator* alloc_{};
    AllocTag       tag_{AllocTag::Unknown};
    void*          storage_{};
    size_t         size_{};
    R    (*invoker_)(const void*, Args...)  {};
    void (*destructor_)(void*)              {};
    void (*copier_)(const void*, void*)     {};
    void (*mover_)(void*, void*)            {};
};
} // Core

#endif //WILL_ENGINE_FUNCTION_H
