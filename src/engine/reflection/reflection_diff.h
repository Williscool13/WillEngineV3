//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_REFLECTION_DIFF_H
#define WILL_ENGINE_REFLECTION_DIFF_H

#include <cstring>
#include <type_traits>
#include <variant>

#include "engine/reflection/reflection.h"

namespace Engine
{
template<typename V>
struct IsVariant : std::false_type
{};

template<typename... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type
{};

template<typename V>
concept IndexedContainer = requires(const V& v) {
    v.Size();
    v[0];
};

template<typename V>
bool DeepEqual(const V& a, const V& b)
{
    if constexpr (Reflected<V>) {
        bool bEqual = true;
        ForEachField<V>([&](const auto& f) { bEqual = bEqual && DeepEqual(a.*f.member, b.*f.member); });
        return bEqual;
    }
    else if constexpr (IsVariant<V>::value) {
        if (a.index() != b.index()) { return false; }
        return std::visit([&b](const auto& av) {
            using Alt = std::decay_t<decltype(av)>;
            return DeepEqual(av, std::get<Alt>(b));
        }, a);
    }
    else if constexpr (std::is_array_v<V>) {
        for (size_t i = 0; i < std::extent_v<V>; ++i) {
            if (!DeepEqual(a[i], b[i])) { return false; }
        }
        return true;
    }
    else if constexpr (requires { a == b; }) {
        return a == b;
    }
    else if constexpr (IndexedContainer<V>) {
        if (a.Size() != b.Size()) { return false; }
        for (size_t i = 0; i < a.Size(); ++i) {
            if (!DeepEqual(a[i], b[i])) { return false; }
        }
        return true;
    }
    else {
        static_assert(std::is_trivially_copyable_v<V>, "DeepEqual needs reflection, operator==, or a trivially copyable type");
        return memcmp(&a, &b, sizeof(V)) == 0;
    }
}

/** A variant recurses only when target holds the same alternative; a changed alternative replaces it whole. */
template<typename V>
void ApplyDiff(V& target, const V& before, const V& after)
{
    if constexpr (Reflected<V>) {
        ForEachField<V>([&](const auto& f) { ApplyDiff(target.*f.member, before.*f.member, after.*f.member); });
    }
    else if constexpr (IsVariant<V>::value) {
        if (before.index() != after.index()) {
            target = after;
            return;
        }
        if (target.index() != after.index()) {
            return;
        }
        std::visit([&](auto& t) {
            using Alt = std::decay_t<decltype(t)>;
            ApplyDiff(t, std::get<Alt>(before), std::get<Alt>(after));
        }, target);
    }
    else if constexpr (std::is_array_v<V>) {
        for (size_t i = 0; i < std::extent_v<V>; ++i) {
            ApplyDiff(target[i], before[i], after[i]);
        }
    }
    else {
        if (!DeepEqual(before, after)) { target = after; }
    }
}
}

#endif //WILL_ENGINE_REFLECTION_DIFF_H
