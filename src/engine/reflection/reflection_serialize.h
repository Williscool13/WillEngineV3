//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_REFLECTION_SERIALIZE_H
#define WILL_ENGINE_REFLECTION_SERIALIZE_H

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <variant>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/array.h"
#include "core/containers/inline_string.h"
#include "engine/reflection/reflection.h"
#include "engine/reflection/reflection_diff.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Engine
{
/** Unsupported types can still be reflected for editing and undo; their owner keeps a hand-written serializer. */
template<typename V>
struct FieldTraits
{
    static constexpr bool SUPPORTED = false;
};

template<Reflected T>
consteval bool AllFieldsSerializable()
{
    bool bAll = true;
    ForEachField<T>([&bAll](const auto& f) { bAll = bAll && FieldTraits<FieldMemberType<decltype(f)>>::SUPPORTED; });
    return bAll;
}

template<typename T>
concept ReflectedSerializable = Reflected<T> && AllFieldsSerializable<T>();

template<ReflectedSerializable T>
void SerializeFields(const T& v, TextWriter& w);

template<ReflectedSerializable T>
void DeserializeFields(T& v, const TextReader& r);

/** Container elements: reflected structs, or any type with FieldTraits. */
template<typename E>
consteval bool ElementSerializable()
{
    if constexpr (Reflected<E>) {
        return AllFieldsSerializable<E>();
    }
    else {
        return FieldTraits<E>::SUPPORTED;
    }
}

template<typename V>
concept IdLike = requires(const V v) {
    { v.id } -> std::convertible_to<uint64_t>;
    V(uint64_t{});
};

template<typename V>
struct DirectFieldTraits
{
    static constexpr bool SUPPORTED = true;

    static void Write(TextWriter& w, const char* key, const V& v) { w.Key(key, v); }
    static bool Equal(const V& a, const V& b) { return a == b; }
};

template<>
struct FieldTraits<float> : DirectFieldTraits<float>
{
    static void Read(const TextReader& r, const char* key, float& v) { v = r.Float(key, v); }
};

template<>
struct FieldTraits<int32_t> : DirectFieldTraits<int32_t>
{
    static void Read(const TextReader& r, const char* key, int32_t& v) { v = r.Int(key, v); }
};

template<>
struct FieldTraits<uint32_t> : DirectFieldTraits<uint32_t>
{
    static void Read(const TextReader& r, const char* key, uint32_t& v) { v = r.UInt(key, v); }
};

template<>
struct FieldTraits<uint64_t> : DirectFieldTraits<uint64_t>
{
    static void Read(const TextReader& r, const char* key, uint64_t& v) { v = r.U64(key, v); }
};

template<>
struct FieldTraits<bool> : DirectFieldTraits<bool>
{
    static void Read(const TextReader& r, const char* key, bool& v) { v = r.Bool(key, v); }
};

template<>
struct FieldTraits<glm::vec2> : DirectFieldTraits<glm::vec2>
{
    static void Read(const TextReader& r, const char* key, glm::vec2& v) { v = r.Vec2(key, v); }
};

template<>
struct FieldTraits<glm::vec3> : DirectFieldTraits<glm::vec3>
{
    static void Read(const TextReader& r, const char* key, glm::vec3& v) { v = r.Vec3(key, v); }
};

template<>
struct FieldTraits<glm::vec4> : DirectFieldTraits<glm::vec4>
{
    static void Read(const TextReader& r, const char* key, glm::vec4& v) { v = r.Vec4(key, v); }
};

template<>
struct FieldTraits<glm::quat> : DirectFieldTraits<glm::quat>
{
    static void Read(const TextReader& r, const char* key, glm::quat& v) { v = r.Quat(key, v); }
};

template<typename V> requires std::is_enum_v<V>
struct FieldTraits<V>
{
    static constexpr bool SUPPORTED = true;

    static void Write(TextWriter& w, const char* key, const V& v) { w.Key(key, static_cast<uint32_t>(v)); }
    static void Read(const TextReader& r, const char* key, V& v) { v = static_cast<V>(r.UInt(key, static_cast<uint32_t>(v))); }
    static bool Equal(const V& a, const V& b) { return a == b; }
};

template<typename V> requires IdLike<V>
struct FieldTraits<V>
{
    static constexpr bool SUPPORTED = true;

    static void Write(TextWriter& w, const char* key, const V& v) { w.Key(key, static_cast<uint64_t>(v.id)); }
    static void Read(const TextReader& r, const char* key, V& v) { v = V(r.U64(key, v.id)); }
    static bool Equal(const V& a, const V& b) { return a.id == b.id; }
};

template<size_t N>
struct FieldTraits<Core::InlineString<N>>
{
    static constexpr bool SUPPORTED = true;

    static void Write(TextWriter& w, const char* key, const Core::InlineString<N>& v) { w.KeyStr(key, v.View()); }
    static void Read(const TextReader& r, const char* key, Core::InlineString<N>& v) { r.Str(key, v); }
    static bool Equal(const Core::InlineString<N>& a, const Core::InlineString<N>& b) { return a == b; }
};

template<size_t N> requires (N >= 2 && N <= 4)
struct FieldTraits<float[N]>
{
    static constexpr bool SUPPORTED = true;

    using Vec = glm::vec<static_cast<glm::length_t>(N), float>;

    static Vec ToVec(const float (&v)[N])
    {
        Vec out;
        for (size_t i = 0; i < N; ++i) { out[static_cast<glm::length_t>(i)] = v[i]; }
        return out;
    }

    static void Write(TextWriter& w, const char* key, const float (&v)[N]) { w.Key(key, ToVec(v)); }

    static void Read(const TextReader& r, const char* key, float (&v)[N])
    {
        Vec out = ToVec(v);
        FieldTraits<Vec>::Read(r, key, out);
        for (size_t i = 0; i < N; ++i) { v[i] = out[static_cast<glm::length_t>(i)]; }
    }

    static bool Equal(const float (&a)[N], const float (&b)[N])
    {
        for (size_t i = 0; i < N; ++i) {
            if (a[i] != b[i]) { return false; }
        }
        return true;
    }
};

/** Opener of each container element block; readers ignore opener names. */
inline constexpr const char* ELEMENT_BLOCK = "item";
/** Key of a non-struct element inside its block. */
inline constexpr const char* ELEMENT_VALUE_KEY = "v";

template<typename E>
void WriteElement(TextWriter& w, const E& e)
{
    w.BeginBlock(ELEMENT_BLOCK);
    if constexpr (Reflected<E>) {
        SerializeFields(e, w);
    }
    else {
        FieldTraits<E>::Write(w, ELEMENT_VALUE_KEY, e);
    }
    w.EndBlock();
}

template<typename E>
void ReadElement(const TextReader& r, E& e)
{
    if constexpr (Reflected<E>) {
        DeserializeFields(e, r);
    }
    else {
        FieldTraits<E>::Read(r, ELEMENT_VALUE_KEY, e);
    }
}

/** Reflected struct: its fields in a block named by the key. */
template<typename V> requires Reflected<V> && (!IdLike<V>)
struct FieldTraits<V>
{
    static constexpr bool SUPPORTED = AllFieldsSerializable<V>();

    static void Write(TextWriter& w, const char* key, const V& v)
    {
        w.BeginBlock(key);
        SerializeFields(v, w);
        w.EndBlock();
    }

    static void Read(const TextReader& r, const char* key, V& v)
    {
        const TextReader block = r.Block(key);
        if (block.IsValid()) { DeserializeFields(v, block); }
    }

    static void WriteFlattened(TextWriter& w, const char*, const V& v) { SerializeFields(v, w); }
    static void ReadFlattened(const TextReader& r, const char*, V& v) { DeserializeFields(v, r); }
    static bool Equal(const V& a, const V& b) { return DeepEqual(a, b); }
};

template<typename A>
consteval bool AlternativeSerializable()
{
    if constexpr (std::is_same_v<A, std::monostate>) {
        return true;
    }
    else if constexpr (Reflected<A>) {
        return AllFieldsSerializable<A>();
    }
    else {
        return false;
    }
}

template<typename V>
void EmplaceVariantIndex(V& v, size_t index)
{
    [&v, index]<size_t... I>(std::index_sequence<I...>) {
        ((I == index ? (void) v.template emplace<I>() : (void) 0), ...);
    }(std::make_index_sequence<std::variant_size_v<V>>{});
}

/** Variant of reflected structs (and monostate): `type|<index>` then the alternative's fields, in a block named by the key. */
template<typename... A>
struct FieldTraits<std::variant<A...>>
{
    using V = std::variant<A...>;

    static constexpr bool SUPPORTED = (AlternativeSerializable<A>() && ...);

    static void WriteFlattened(TextWriter& w, const char* key, const V& v)
    {
        w.Key(key, static_cast<uint32_t>(v.index()));
        std::visit([&w](const auto& alt) {
            if constexpr (Reflected<std::decay_t<decltype(alt)>>) {
                SerializeFields(alt, w);
            }
        }, v);
    }

    static void ReadFlattened(const TextReader& r, const char* key, V& v)
    {
        if (!r.Has(key)) { return; }
        EmplaceVariantIndex(v, r.UInt(key));
        std::visit([&r](auto& alt) {
            if constexpr (Reflected<std::decay_t<decltype(alt)>>) {
                DeserializeFields(alt, r);
            }
        }, v);
    }

    static void Write(TextWriter& w, const char* key, const V& v)
    {
        w.BeginBlock(key);
        WriteFlattened(w, "type", v);
        w.EndBlock();
    }

    static void Read(const TextReader& r, const char* key, V& v)
    {
        const TextReader block = r.Block(key);
        if (block.IsValid()) { ReadFlattened(block, "type", v); }
    }

    static bool Equal(const V& a, const V& b) { return DeepEqual(a, b); }
};

template<typename V>
struct IsInlineString : std::false_type
{};

template<size_t N>
struct IsInlineString<Core::InlineString<N>> : std::true_type
{};

template<typename V>
concept GrowableContainer = !IsInlineString<V>::value && requires(V& v, const V& cv) {
    v.Clear();
    v.PushBack(cv[0]);
    cv.Size();
};

/** Growable container: `key|<count>` followed by one block per element. */
template<typename V> requires GrowableContainer<V>
struct FieldTraits<V>
{
    using E = std::remove_cvref_t<decltype(std::declval<const V&>()[0])>;

    static constexpr bool SUPPORTED = ElementSerializable<E>();

    static void Write(TextWriter& w, const char* key, const V& v)
    {
        w.Count(key, static_cast<uint32_t>(v.Size()));
        for (size_t i = 0; i < v.Size(); ++i) {
            WriteElement(w, v[i]);
        }
    }

    static void Read(const TextReader& r, const char* key, V& v)
    {
        if (!r.Has(key)) { return; }
        v.Clear();
        r.ForEachRecord(key, [&v](const TextReader& block) {
            if constexpr (requires { v.IsFull(); }) {
                if (v.IsFull()) { return; }
            }
            E e{};
            ReadElement(block, e);
            v.PushBack(std::move(e));
        });
    }

    static bool Equal(const V& a, const V& b) { return DeepEqual(a, b); }
};

/** Fixed-size array: like a container, with trailing default elements left out. */
template<typename V, typename E, size_t N>
struct FixedArrayFieldTraits
{
    static constexpr bool SUPPORTED = ElementSerializable<E>();

    static void Write(TextWriter& w, const char* key, const V& v)
    {
        static const E DEF{};
        size_t count = N;
        while (count > 0 && !w.WritesDefaults() && DeepEqual(v[count - 1], DEF)) { --count; }
        w.Count(key, static_cast<uint32_t>(count));
        for (size_t i = 0; i < count; ++i) {
            WriteElement(w, v[i]);
        }
    }

    static void Read(const TextReader& r, const char* key, V& v)
    {
        if (!r.Has(key)) { return; }
        size_t count = 0;
        r.ForEachRecord(key, [&v, &count](const TextReader& block) {
            if (count >= N) { return; }
            E e{};
            ReadElement(block, e);
            v[count++] = e;
        });
        for (; count < N; ++count) { v[count] = E{}; }
    }

    static bool Equal(const V& a, const V& b) { return DeepEqual(a, b); }
};

template<typename E, size_t N> requires (!std::is_same_v<E, float> || N < 2 || N > 4)
struct FieldTraits<E[N]> : FixedArrayFieldTraits<E[N], E, N>
{};

template<typename E, size_t N>
struct FieldTraits<Core::Array<E, N>> : FixedArrayFieldTraits<Core::Array<E, N>, E, N>
{};

template<typename V>
concept HasFlattenTraits = requires(TextWriter& w, const TextReader& r, V& v) {
    FieldTraits<V>::WriteFlattened(w, "", v);
    FieldTraits<V>::ReadFlattened(r, "", v);
};

/** Fields equal to T{} are omitted unless FIELD_ALWAYS_WRITE. */
template<ReflectedSerializable T>
void SerializeFields(const T& v, TextWriter& w)
{
    static const T DEF{};
    ForEachField<T>([&v, &w](const auto& f) {
        using M = FieldMemberType<decltype(f)>;
        if (!w.WritesDefaults() && !f.Has(FIELD_ALWAYS_WRITE) && FieldTraits<M>::Equal(v.*f.member, DEF.*f.member)) { return; }
        if constexpr (HasFlattenTraits<M>) {
            if (f.Has(FIELD_FLATTEN)) {
                FieldTraits<M>::WriteFlattened(w, f.Key(), v.*f.member);
                return;
            }
        }
        FieldTraits<M>::Write(w, f.Key(), v.*f.member);
    });
}

template<ReflectedSerializable T>
void DeserializeFields(T& v, const TextReader& r)
{
    ForEachField<T>([&v, &r](const auto& f) {
        using M = FieldMemberType<decltype(f)>;
        if constexpr (HasFlattenTraits<M>) {
            if (f.Has(FIELD_FLATTEN)) {
                FieldTraits<M>::ReadFlattened(r, f.Key(), v.*f.member);
                return;
            }
        }
        FieldTraits<M>::Read(r, f.Key(), v.*f.member);
    });
    if constexpr (HasSanitize<T>) {
        T::Sanitize(v);
    }
}
}

#endif //WILL_ENGINE_REFLECTION_SERIALIZE_H
