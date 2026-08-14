//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_TEXT_WRITER_H
#define WILL_ENGINE_TEXT_WRITER_H

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/containers/vector.h"
#include "engine/serialization/text_parse.h"

namespace Engine
{
/**
 * Line-oriented serializer for asset text bodies (docs/serialization/text_format.md).
 * Appends `key|value` field lines and opener/`;` block lines to a caller-owned buffer that is reused across a whole save. Floats are IEEE-754 bits in hex.
 */
class TextWriter
{
public:
    explicit TextWriter(Core::Vector<std::byte>& _out) : out(&_out) {}

    void Key(const char* key, float v) { AppendTextF(*out, "%s|0x%08x\n", key, FloatBits(v)); }
    void Key(const char* key, int32_t v) { AppendTextF(*out, "%s|%d\n", key, v); }
    void Key(const char* key, uint32_t v) { AppendTextF(*out, "%s|%u\n", key, v); }
    void Key(const char* key, int64_t v) { AppendTextF(*out, "%s|%lld\n", key, static_cast<long long>(v)); }
    void Key(const char* key, uint64_t v) { AppendTextF(*out, "%s|%llu\n", key, static_cast<unsigned long long>(v)); }
    void Key(const char* key, bool v) { AppendTextF(*out, "%s|%u\n", key, v ? 1u : 0u); }
    void Key(const char* key, const glm::vec2& v) { AppendTextF(*out, "%s|0x%08x|0x%08x\n", key, FloatBits(v.x), FloatBits(v.y)); }
    void Key(const char* key, const glm::vec3& v) { AppendTextF(*out, "%s|0x%08x|0x%08x|0x%08x\n", key, FloatBits(v.x), FloatBits(v.y), FloatBits(v.z)); }
    void Key(const char* key, const glm::vec4& v) { AppendTextF(*out, "%s|0x%08x|0x%08x|0x%08x|0x%08x\n", key, FloatBits(v.x), FloatBits(v.y), FloatBits(v.z), FloatBits(v.w)); }
    void Key(const char* key, const glm::ivec4& v) { AppendTextF(*out, "%s|%d|%d|%d|%d\n", key, v.x, v.y, v.z, v.w); }

    /** Stored w,x,y,z. */
    void Key(const char* key, const glm::quat& v) { AppendTextF(*out, "%s|0x%08x|0x%08x|0x%08x|0x%08x\n", key, FloatBits(v.w), FloatBits(v.x), FloatBits(v.y), FloatBits(v.z)); }

    /** Escapes backslash and newlines; the value may freely contain '|'. */
    void KeyStr(const char* key, std::string_view s)
    {
        AppendText(*out, key);
        PushChar('|');
        for (const char c : s) {
            if (c == '\\') { AppendText(*out, "\\\\"); }
            else if (c == '\n') { AppendText(*out, "\\n"); }
            else if (c == '\r') { AppendText(*out, "\\r"); }
            else { PushChar(c); }
        }
        PushChar('\n');
    }

    /** Variable-length uint list on one line: `key|v0|v1|...`. Omit entirely for an empty list. */
    void KeyUInts(const char* key, const uint32_t* v, size_t n)
    {
        AppendText(*out, key);
        for (size_t i = 0; i < n; ++i) {
            AppendTextF(*out, "|%u", v[i]);
        }
        PushChar('\n');
    }

    /** Writes nothing when v equals def; the reader restores def for the absent key. */
    template<typename T>
    void KeyOpt(const char* key, const T& v, const T& def)
    {
        if (!(v == def)) { Key(key, v); }
    }

    /** Array count field; follow with exactly n record blocks. Omit entirely for an empty array. */
    void Count(const char* key, uint32_t n) { Key(key, n); }

    /** Splices pre-serialized fragment bytes verbatim (e.g. a component fragment written through another TextWriter). */
    void Raw(const void* data, size_t n)
    {
        const auto* p = static_cast<const std::byte*>(data);
        out->Append(p, p + n);
    }

    void BeginBlock(const char* token) { AppendTextF(*out, "%s\n", token); }
    void BeginBlock(uint64_t token) { AppendTextF(*out, "%llu\n", static_cast<unsigned long long>(token)); }
    void EndBlock() { AppendText(*out, ";\n"); }

private:
    Core::Vector<std::byte>* out;

    void PushChar(char c) { out->PushBack(static_cast<std::byte>(c)); }
};
} // Engine

#endif //WILL_ENGINE_TEXT_WRITER_H
