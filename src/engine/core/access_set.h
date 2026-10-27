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
inline constexpr uint32_t MAX_ACCESS_ENTRIES = 16;

struct AccessSet
{
    bool bExclusive{false};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> reads{};
    Core::InlineVector<StringID, MAX_ACCESS_ENTRIES> writes{};

    static const AccessSet EXCLUSIVE;
};

inline const AccessSet AccessSet::EXCLUSIVE{.bExclusive = true};

template<typename... T>
struct Reads
{};

template<typename... T>
struct Writes
{};

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

template<typename... Lists>
AccessSet Access()
{
    AccessSet set{};
    (AppendAccess(set, Lists{}), ...);
    return set;
}
} // Engine

#endif //WILL_ENGINE_ACCESS_SET_H
