//
// Created by William on 2026-04-02.
//

#ifndef WILL_ENGINE_INLINE_FUNCTION_H
#define WILL_ENGINE_INLINE_FUNCTION_H

#include <cassert>
#include <cstring>
#include <type_traits>
#include <utility>

namespace Core
{
template<typename Sig, size_t Size = 64>
class InlineFunction;

template<typename R, typename... Args, size_t Size>
class InlineFunction<R(Args...), Size>
{
public:
    InlineFunction() = default;

    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, InlineFunction>> >
    InlineFunction(F&& f)
    {
        using FD = std::decay_t<F>;
        static_assert(sizeof(FD) <= Size, "callable exceeds InlineFunction buffer");
        static_assert(alignof(FD) <= alignof(std::max_align_t), "callable alignment exceeds InlineFunction storage alignment");
        new(storage_) FD(std::forward<F>(f));
        invoker_ = [](const char* s, Args... a) -> R { return (*reinterpret_cast<const FD*>(s))(std::forward<Args>(a)...); };
        destructor_ = [](char* s) { reinterpret_cast<FD*>(s)->~FD(); };
        copier_ = [](const char* src, char* dst) { new(dst) FD(*reinterpret_cast<const FD*>(src)); };
        mover_ = [](char* src, char* dst) { new(dst) FD(std::move(*reinterpret_cast<FD*>(src))); };
    }

    ~InlineFunction() { if (destructor_) { destructor_(storage_); } }

    InlineFunction(const InlineFunction& other)
        : invoker_(other.invoker_), destructor_(other.destructor_), copier_(other.copier_), mover_(other.mover_)
    {
        if (copier_) { copier_(other.storage_, storage_); }
    }

    InlineFunction& operator=(const InlineFunction& other)
    {
        if (this != &other) {
            if (destructor_) { destructor_(storage_); }
            invoker_ = other.invoker_;
            destructor_ = other.destructor_;
            copier_ = other.copier_;
            mover_ = other.mover_;
            if (copier_) { copier_(other.storage_, storage_); }
        }
        return *this;
    }

    InlineFunction(InlineFunction&& other) noexcept
        : invoker_(other.invoker_), destructor_(other.destructor_), copier_(other.copier_), mover_(other.mover_)
    {
        if (mover_) { mover_(other.storage_, storage_); }
        other.invoker_ = nullptr;
        other.destructor_ = nullptr;
        other.copier_ = nullptr;
        other.mover_ = nullptr;
    }

    InlineFunction& operator=(InlineFunction&& other) noexcept
    {
        if (this != &other) {
            if (destructor_) { destructor_(storage_); }
            invoker_ = other.invoker_;
            destructor_ = other.destructor_;
            copier_ = other.copier_;
            mover_ = other.mover_;
            if (mover_) { mover_(other.storage_, storage_); }
            other.invoker_ = nullptr;
            other.destructor_ = nullptr;
            other.copier_ = nullptr;
            other.mover_ = nullptr;
        }
        return *this;
    }

    R operator()(Args... args) const
    {
        assert(invoker_ && "InlineFunction called while empty");
        return invoker_(storage_, std::forward<Args>(args)...);
    }

    explicit operator bool() const { return invoker_ != nullptr; }

private:
    alignas(std::max_align_t) char storage_[Size]{};

    R (*invoker_)(const char*, Args...){};

    void (*destructor_)(char*){};

    void (*copier_)(const char*, char*){};

    void (*mover_)(char*, char*){};
};

} // Core

#endif //WILL_ENGINE_INLINE_FUNCTION_H
