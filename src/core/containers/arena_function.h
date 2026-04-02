//
// Created by William on 2026-04-02.
//

#ifndef WILL_ENGINE_ARENA_FUNCTION_H
#define WILL_ENGINE_ARENA_FUNCTION_H

#include <cassert>
#include <type_traits>
#include <utility>

#include "core/memory/arena.h"

namespace Core
{
template<typename Sig>
class ArenaFunction;

template<typename R, typename... Args>
class ArenaFunction<R(Args...)>
{
public:
    ArenaFunction() = default;

    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, ArenaFunction>>>
    ArenaFunction(F&& f, Arena* arena)
    {
        using FD = std::decay_t<F>;
        storage_ = arena->AllocRaw(sizeof(FD), alignof(FD));
        assert(storage_ != nullptr && "ArenaFunction: allocation failed");
        new(storage_) FD(std::forward<F>(f));
        invoker_    = [](const void* s, Args... a) -> R { return (*static_cast<const FD*>(s))(std::forward<Args>(a)...); };
        destructor_ = [](void* s) { static_cast<FD*>(s)->~FD(); };
    }

    ~ArenaFunction()
    {
        if (storage_ && destructor_) { destructor_(storage_); }
    }

    ArenaFunction(const ArenaFunction&)            = delete;
    ArenaFunction& operator=(const ArenaFunction&) = delete;

    ArenaFunction(ArenaFunction&& other) noexcept
        : storage_(other.storage_), invoker_(other.invoker_), destructor_(other.destructor_)
    {
        other.storage_    = nullptr;
        other.invoker_    = nullptr;
        other.destructor_ = nullptr;
    }

    ArenaFunction& operator=(ArenaFunction&& other) noexcept
    {
        if (this != &other) {
            if (storage_ && destructor_) { destructor_(storage_); }
            storage_    = other.storage_;
            invoker_    = other.invoker_;
            destructor_ = other.destructor_;
            other.storage_    = nullptr;
            other.invoker_    = nullptr;
            other.destructor_ = nullptr;
        }
        return *this;
    }

    R operator()(Args... args) const
    {
        assert(invoker_ && "ArenaFunction called while empty");
        return invoker_(storage_, std::forward<Args>(args)...);
    }

    explicit operator bool() const { return invoker_ != nullptr; }

private:
    void* storage_{};
    R    (*invoker_)(const void*, Args...)  {};
    void (*destructor_)(void*)              {};
};
} // Core

#endif //WILL_ENGINE_ARENA_FUNCTION_H
