//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_REFLECTION_ENTITY_REFS_H
#define WILL_ENGINE_REFLECTION_ENTITY_REFS_H

#include <type_traits>
#include <utility>
#include <variant>

#include "engine/core/entity_ref.h"
#include "engine/reflection/reflection.h"
#include "engine/reflection/reflection_diff.h"

namespace Engine
{
template<typename V>
consteval bool ContainsEntityRef();

template<typename... A>
consteval bool AnyContainsEntityRef(std::variant<A...>*)
{
    return (ContainsEntityRef<A>() || ...);
}

template<typename V>
consteval bool ContainsEntityRef()
{
    if constexpr (std::is_same_v<V, EntityRef>) {
        return true;
    }
    else if constexpr (Reflected<V>) {
        bool bAny = false;
        ForEachField<V>([&bAny](const auto& f) { bAny = bAny || ContainsEntityRef<FieldMemberType<decltype(f)>>(); });
        return bAny;
    }
    else if constexpr (IsVariant<V>::value) {
        return AnyContainsEntityRef(static_cast<V*>(nullptr));
    }
    else if constexpr (std::is_array_v<V>) {
        return ContainsEntityRef<std::remove_extent_t<V>>();
    }
    else if constexpr (IndexedContainer<V>) {
        return ContainsEntityRef<std::remove_cvref_t<decltype(std::declval<const V&>()[0])>>();
    }
    else {
        return false;
    }
}

/** Calls fn(EntityRef&) for every EntityRef in v, through reflected structs, variants, arrays and containers. */
template<typename V, typename Fn>
void ForEachEntityRef(V& v, Fn&& fn)
{
    if constexpr (!ContainsEntityRef<V>()) {
        return;
    }
    else if constexpr (std::is_same_v<V, EntityRef>) {
        fn(v);
    }
    else if constexpr (Reflected<V>) {
        ForEachField<V>([&v, &fn](const auto& f) { ForEachEntityRef(v.*f.member, fn); });
    }
    else if constexpr (IsVariant<V>::value) {
        std::visit([&fn](auto& alt) { ForEachEntityRef(alt, fn); }, v);
    }
    else if constexpr (std::is_array_v<V>) {
        for (auto& e : v) { ForEachEntityRef(e, fn); }
    }
    else {
        for (size_t i = 0; i < v.Size(); ++i) { ForEachEntityRef(v[i], fn); }
    }
}
} // Engine

#endif //WILL_ENGINE_REFLECTION_ENTITY_REFS_H
