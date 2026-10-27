//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_CHECKED_H
#define WILL_ENGINE_CHECKED_H

#include "engine/components/component_identity.h"
#include "engine/core/access_check.h"

namespace Engine
{
/** A service pointer whose every use counts as touching T for the running system's declared access; T needs a COMPONENT_NAME. */
template<typename T>
class Checked
{
public:
    Checked() = default;
    explicit Checked(T* ptr_) : ptr(ptr_) {}

    Checked& operator=(T* ptr_)
    {
        ptr = ptr_;
        return *this;
    }

    T* operator->() const
    {
        Check();
        return ptr;
    }

    T& operator*() const
    {
        Check();
        return *ptr;
    }

    explicit operator T*() const
    {
        Check();
        return ptr;
    }

private:
    static void Check()
    {
#if WILL_ACCESS_CHECK
        CheckReadAccess(AccessId<T>());
#endif
    }

    T* ptr{nullptr};
};
} // Engine

#endif //WILL_ENGINE_CHECKED_H
