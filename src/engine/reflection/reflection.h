//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_REFLECTION_H
#define WILL_ENGINE_REFLECTION_H

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Engine
{
struct FieldAttrs
{
    const char* key{nullptr};
    float min{0.0f};
    float max{0.0f};
    float speed{0.0f};
};

template<typename Owner, typename Member>
struct Field
{
    using OwnerType = Owner;
    using MemberType = Member;

    const char* name;
    Member Owner::* member;
    FieldAttrs attrs;

    [[nodiscard]] constexpr const char* Key() const { return attrs.key ? attrs.key : name; }
};

template<typename T>
concept Reflected = requires { T::Fields(); };

/** Optional; runs after every read and edit. */
template<typename T>
concept HasSanitize = requires(T& v) { T::Sanitize(v); };

template<Reflected T>
inline constexpr size_t FIELD_COUNT = std::tuple_size_v<decltype(T::Fields())>;

template<typename F>
using FieldMemberType = typename std::decay_t<F>::MemberType;

template<Reflected T, typename Fn>
constexpr void ForEachField(Fn&& fn)
{
    std::apply([&fn](const auto&... f) { (fn(f), ...); }, T::Fields());
}

template<Reflected T, typename Fn>
constexpr void ForEachFieldIndexed(Fn&& fn)
{
    [&fn]<size_t... I>(std::index_sequence<I...>) {
        constexpr auto fields = T::Fields();
        (fn(std::get<I>(fields), static_cast<uint32_t>(I)), ...);
    }(std::make_index_sequence<FIELD_COUNT<T>>{});
}

inline constexpr uint32_t INVALID_FIELD_INDEX = UINT32_MAX;

template<Reflected T, typename M>
constexpr uint32_t FieldIndexOf(M T::* member)
{
    uint32_t result = INVALID_FIELD_INDEX;
    ForEachFieldIndexed<T>([&result, member](const auto& f, uint32_t index) {
        if constexpr (std::is_same_v<FieldMemberType<decltype(f)>, M>) {
            if (f.member == member) { result = index; }
        }
    });
    return result;
}

template<Reflected T, typename Fn>
constexpr void VisitField(uint32_t index, Fn&& fn)
{
    ForEachFieldIndexed<T>([index, &fn](const auto& f, uint32_t i) {
        if (i == index) { fn(f); }
    });
}
}

/** e.g. WILL_FIELD(radius, .min = 0.0f, .speed = 0.01f) */
#define WILL_FIELD(member, ...) ::Engine::Field<Self, decltype(Self::member)>{#member, &Self::member, ::Engine::FieldAttrs{__VA_ARGS__}}

/** Inside the struct body; fields in serialized order. */
#define WILL_REFLECT(Type, ...) \
    using Self = Type; \
    static constexpr auto Fields() { return std::make_tuple(__VA_ARGS__); }

#endif //WILL_ENGINE_REFLECTION_H
