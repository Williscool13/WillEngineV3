//
// Created by William on 2026-10-28.
//

#ifndef WILL_ENGINE_ACCESS_SET_H
#define WILL_ENGINE_ACCESS_SET_H

#include <cstdint>
#include <type_traits>

#include "core/string_id.h"
#include "core/containers/inline_vector.h"
#include "engine/components/component_identity.h"

namespace Engine
{
inline constexpr uint32_t MAX_ACCESS_ENTRIES = 32;
inline constexpr uint32_t MAX_AFTER_ENTRIES = 4;

/**
 * Writes conflict with every other access to the same id. DeferredWrites apply after the wave in registration order, so they do not conflict
 * with each other, only with Reads and Writes. After names earlier-registered systems that must finish first.
 * Engine state only needs a name if two systems in one phase touch it and one writes. State that only exclusive systems write stays undeclared; they run alone.
 */
struct AccessSet
{
    bool bExclusive{false};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> reads{};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> writes{};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> deferredWrites{};
    Core::InlineVector<const void*, MAX_AFTER_ENTRIES> after{};

    static const AccessSet EXCLUSIVE;
};

inline const AccessSet AccessSet::EXCLUSIVE{.bExclusive = true};

/** A type's `using ACCESS_PARTS = AccessParts<...>`: accessing it is accessing all of these. Declare a part instead to overlap with systems touching the others. */
template<typename... T>
struct AccessParts
{};

template<typename T>
void PushAccess(Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& list);

template<typename... T>
void PushAccessParts(Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& list, AccessParts<T...>)
{
    (PushAccess<T>(list), ...);
}

template<typename T>
void PushAccess(Core::InlineVector<StringID, MAX_ACCESS_ENTRIES>& list)
{
    list.PushBack(AccessId<T>());
    if constexpr (requires { typename T::ACCESS_PARTS; }) { PushAccessParts(list, typename T::ACCESS_PARTS{}); }
}

template<typename... T>
struct Reads
{};

template<typename... T>
struct Writes
{};

/** Get and Set/Remove only; GetMut needs Writes. */
template<typename... T>
struct DeferredWrites
{};

template<auto... Fns>
struct After
{};

template<typename Fn>
const void* SystemId(Fn fn)
{
    return reinterpret_cast<const void*>(fn);
}

template<typename... T>
void AppendAccess(AccessSet& set, Reads<T...>)
{
    (PushAccess<std::remove_cv_t<T>>(set.reads), ...);
}

template<typename... T>
void AppendAccess(AccessSet& set, Writes<T...>)
{
    (PushAccess<std::remove_cv_t<T>>(set.writes), ...);
}

template<typename... T>
void AppendAccess(AccessSet& set, DeferredWrites<T...>)
{
    (PushAccess<std::remove_cv_t<T>>(set.deferredWrites), ...);
}

template<auto... Fns>
void AppendAccess(AccessSet& set, After<Fns...>)
{
    (set.after.PushBack(SystemId(Fns)), ...);
}

template<typename... Lists>
AccessSet Access()
{
    AccessSet set{};
    (AppendAccess(set, Lists{}), ...);
    return set;
}
} // Engine

#endif //WILL_ENGINE_ACCESS_SET_H
