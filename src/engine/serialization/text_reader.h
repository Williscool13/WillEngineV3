//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_TEXT_READER_H
#define WILL_ENGINE_TEXT_READER_H

#include <charconv>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/inline_string.h"
#include "engine/serialization/text_parse.h"

namespace Engine
{
/**
 * Zero-copy reader over a text body span (docs/serialization/text_format.md), typically a range inside a Platform::ScopedFileMapping — keep the mapping alive for the reader's scope.
 * Key lookups scan this block's top level; nested blocks are skipped whole. Absent keys yield the supplied default.
 */
class TextReader
{
public:
    TextReader() = default;
    TextReader(const char* _begin, const char* _end) : begin(_begin), end(_end) {}
    TextReader(const void* data, uint64_t size) : begin(static_cast<const char*>(data)), end(static_cast<const char*>(data) + size) {}

    bool IsValid() const { return begin != nullptr; }

    /** This reader's full text span; serialization is deterministic, so equal spans mean equal content (prefab diff). */
    std::string_view Span() const { return {begin, static_cast<size_t>(end - begin)}; }

    bool Has(const char* key) const { return FindValue(key).data() != nullptr; }
    bool HasBlock(const char* key) const { return Block(key).IsValid(); }

    float Float(const char* key, float def = 0.0f) const
    {
        const std::string_view v = FindValue(key);
        return v.data() ? ParseHexFloat(v.data(), v.data() + v.size()) : def;
    }

    int32_t Int(const char* key, int32_t def = 0) const { return ParseDecimal(key, def); }
    uint32_t UInt(const char* key, uint32_t def = 0) const { return ParseDecimal(key, def); }
    int64_t I64(const char* key, int64_t def = 0) const { return ParseDecimal(key, def); }
    uint64_t U64(const char* key, uint64_t def = 0) const { return ParseDecimal(key, def); }

    bool Bool(const char* key, bool def = false) const { return ParseDecimal<uint32_t>(key, def ? 1u : 0u) != 0; }

    glm::vec2 Vec2(const char* key, const glm::vec2& def = glm::vec2(0.0f)) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        float f[2];
        ParseFloatN(v, f, 2);
        return {f[0], f[1]};
    }

    glm::vec3 Vec3(const char* key, const glm::vec3& def = glm::vec3(0.0f)) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        float f[3];
        ParseFloatN(v, f, 3);
        return {f[0], f[1], f[2]};
    }

    glm::vec4 Vec4(const char* key, const glm::vec4& def = glm::vec4(0.0f)) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        float f[4];
        ParseFloatN(v, f, 4);
        return {f[0], f[1], f[2], f[3]};
    }

    glm::ivec4 IVec4(const char* key, const glm::ivec4& def = glm::ivec4(0)) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        int32_t i[4];
        ParseIntN(v, i, 4);
        return {i[0], i[1], i[2], i[3]};
    }

    /** Stored w,x,y,z. */
    glm::quat Quat(const char* key, const glm::quat& def = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        float f[4];
        ParseFloatN(v, f, 4);
        return {f[0], f[1], f[2], f[3]};
    }

    /** Unescapes into outStr; leaves outStr untouched when the key is absent. Returns presence. */
    template<size_t N>
    bool Str(const char* key, Core::InlineString<N>& outStr) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return false; }
        outStr.Clear();
        for (size_t i = 0; i < v.size(); ++i) {
            char c = v[i];
            if (c == '\\' && i + 1 < v.size()) {
                ++i;
                c = v[i] == 'n' ? '\n' : v[i] == 'r' ? '\r' : v[i];
            }
            outStr.Append(&c, 1);
        }
        return true;
    }

    /** Variable-length uint list `key|v0|v1|...`: invokes fn(uint32_t) per value. Absent key invokes nothing. */
    template<typename Fn>
    void ForEachUInt(const char* key, Fn&& fn) const
    {
        std::string_view v = FindValue(key);
        while (v.data()) {
            const size_t bar = v.find('|');
            const std::string_view c = bar == std::string_view::npos ? v : v.substr(0, bar);
            uint32_t value = 0;
            std::from_chars(c.data(), c.data() + c.size(), value);
            fn(value);
            if (bar == std::string_view::npos) { break; }
            v = v.substr(bar + 1);
        }
    }

    /** Raw value text after the first '|' (still escaped); data() is null when absent. */
    std::string_view Raw(const char* key) const { return FindValue(key); }

    /** Named top-level child block; an invalid reader when absent. */
    TextReader Block(const char* key) const
    {
        const size_t keyLen = strlen(key);
        const char* c = begin;
        while (c < end) {
            const LineView lv = ReadLine(c, end);
            if (IsOpener(lv)) {
                const BlockSpan span = FindBlockEnd(lv.next, end);
                if (static_cast<size_t>(lv.e - lv.s) == keyLen && memcmp(lv.s, key, keyLen) == 0) {
                    return {lv.next, span.bodyEnd};
                }
                c = span.next;
            }
            else { c = lv.next; }
        }
        return {};
    }

    uint32_t RecordCount(const char* key) const { return UInt(key); }

    /** Array `key|N`: invokes fn(const TextReader&) for each of the N record blocks following the count line. Record opener names are ignored. */
    template<typename Fn>
    void ForEachRecord(const char* key, Fn&& fn) const
    {
        const size_t keyLen = strlen(key);
        const char* c = begin;
        uint32_t remaining = 0;
        while (c < end) {
            const LineView lv = ReadLine(c, end);
            if (lv.bField) {
                const char* bar = static_cast<const char*>(memchr(lv.s, '|', static_cast<size_t>(lv.e - lv.s)));
                c = lv.next;
                if (static_cast<size_t>(bar - lv.s) == keyLen && memcmp(lv.s, key, keyLen) == 0) {
                    std::from_chars(bar + 1, lv.e, remaining);
                    break;
                }
            }
            else if (IsOpener(lv)) { c = FindBlockEnd(lv.next, end).next; }
            else { c = lv.next; }
        }
        while (remaining > 0 && c < end) {
            const LineView lv = ReadLine(c, end);
            if (IsOpener(lv)) {
                const BlockSpan span = FindBlockEnd(lv.next, end);
                fn(TextReader(lv.next, span.bodyEnd));
                c = span.next;
                --remaining;
            }
            else { c = lv.next; }
        }
    }

    /** Every top-level block in order: fn(std::string_view opener, const TextReader&). */
    template<typename Fn>
    void ForEachBlock(Fn&& fn) const
    {
        const char* c = begin;
        while (c < end) {
            const LineView lv = ReadLine(c, end);
            if (IsOpener(lv)) {
                const BlockSpan span = FindBlockEnd(lv.next, end);
                fn(std::string_view(lv.s, static_cast<size_t>(lv.e - lv.s)), TextReader(lv.next, span.bodyEnd));
                c = span.next;
            }
            else { c = lv.next; }
        }
    }

private:
    const char* begin{nullptr};
    const char* end{nullptr};

    struct LineView
    {
        const char* s;
        const char* e;
        const char* next;
        bool bField;
        bool bClose;
        bool bBlank;
    };

    struct BlockSpan
    {
        const char* bodyEnd;
        const char* next;
    };

    static LineView ReadLine(const char* c, const char* dataEnd)
    {
        const void* nl = memchr(c, '\n', static_cast<size_t>(dataEnd - c));
        const char* le = nl ? static_cast<const char*>(nl) : dataEnd;
        const char* next = nl ? le + 1 : dataEnd;
        if (le > c && le[-1] == '\r') { --le; }
        LineView lv{c, le, next, false, false, false};
        const size_t len = static_cast<size_t>(le - c);
        if (len == 0) { lv.bBlank = true; }
        else if (memchr(c, '|', len)) { lv.bField = true; }
        else if (len == 1 && *c == ';') { lv.bClose = true; }
        return lv;
    }

    static bool IsOpener(const LineView& lv) { return !lv.bField && !lv.bClose && !lv.bBlank; }

    static BlockSpan FindBlockEnd(const char* c, const char* dataEnd)
    {
        int32_t depth = 0;
        while (c < dataEnd) {
            const LineView lv = ReadLine(c, dataEnd);
            if (lv.bClose) {
                if (depth == 0) { return {lv.s, lv.next}; }
                --depth;
            }
            else if (IsOpener(lv)) { ++depth; }
            c = lv.next;
        }
        return {dataEnd, dataEnd};
    }

    std::string_view FindValue(const char* key) const
    {
        const size_t keyLen = strlen(key);
        const char* c = begin;
        while (c < end) {
            const LineView lv = ReadLine(c, end);
            if (lv.bField) {
                const char* bar = static_cast<const char*>(memchr(lv.s, '|', static_cast<size_t>(lv.e - lv.s)));
                if (static_cast<size_t>(bar - lv.s) == keyLen && memcmp(lv.s, key, keyLen) == 0) {
                    return {bar + 1, static_cast<size_t>(lv.e - (bar + 1))};
                }
                c = lv.next;
            }
            else if (IsOpener(lv)) { c = FindBlockEnd(lv.next, end).next; }
            else { c = lv.next; }
        }
        return {};
    }

    template<typename T>
    T ParseDecimal(const char* key, T def) const
    {
        const std::string_view v = FindValue(key);
        if (!v.data()) { return def; }
        T value = def;
        std::from_chars(v.data(), v.data() + v.size(), value);
        return value;
    }

    static void ParseFloatN(std::string_view v, float* outF, int32_t count)
    {
        for (int32_t i = 0; i < count; ++i) {
            const size_t bar = v.find('|');
            const std::string_view c = bar == std::string_view::npos ? v : v.substr(0, bar);
            outF[i] = ParseHexFloat(c.data(), c.data() + c.size());
            v = bar == std::string_view::npos ? std::string_view{} : v.substr(bar + 1);
        }
    }

    static void ParseIntN(std::string_view v, int32_t* outI, int32_t count)
    {
        for (int32_t i = 0; i < count; ++i) {
            const size_t bar = v.find('|');
            const std::string_view c = bar == std::string_view::npos ? v : v.substr(0, bar);
            outI[i] = 0;
            std::from_chars(c.data(), c.data() + c.size(), outI[i]);
            v = bar == std::string_view::npos ? std::string_view{} : v.substr(bar + 1);
        }
    }
};
} // Engine

#endif //WILL_ENGINE_TEXT_READER_H
