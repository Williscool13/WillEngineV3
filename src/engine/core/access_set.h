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
    (set.reads.PushBack(AccessId<std::remove_cv_t<T>>()), ...);
}

template<typename... T>
void AppendAccess(AccessSet& set, Writes<T...>)
{
    (set.writes.PushBack(AccessId<std::remove_cv_t<T>>()), ...);
}

template<typename... T>
void AppendAccess(AccessSet& set, DeferredWrites<T...>)
{
    (set.deferredWrites.PushBack(AccessId<std::remove_cv_t<T>>()), ...);
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
