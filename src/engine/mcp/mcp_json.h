//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_MCP_JSON_H
#define WILL_ENGINE_MCP_JSON_H

#include <cstdint>

#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"

namespace Core
{
class TlsfAllocator;
}

namespace Engine::MCP
{
enum class JsonType : uint8_t
{
    Invalid,
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct JsonToken
{
    JsonType type{JsonType::Invalid};
    /** String: unescaped and NUL-terminated in the source buffer. Number: raw digits, not terminated, use length. */
    const char* text{};
    uint32_t length{0};
    int32_t firstChild{-1};
    int32_t next{-1};
    int32_t childCount{0};
};

class JsonWriter;

/**
 * In-place JSON parser: strings are unescaped and NUL-terminated inside the buffer handed to Parse, which must outlive the reader.
 * Object children alternate key, value along the sibling chain.
 */
class JsonReader
{
public:
    static constexpr int32_t MAX_DEPTH = 32;

    struct Cursor
    {
        char* p{};
        char* end{};
    };

    JsonReader() = default;

    explicit JsonReader(Core::TlsfAllocator* allocator);

    bool Parse(char* text, size_t length);

    [[nodiscard]] int32_t Root() const { return tokens.IsEmpty() ? -1 : 0; }

    [[nodiscard]] const JsonToken* Get(int32_t index) const { return index >= 0 && static_cast<size_t>(index) < tokens.Size() ? &tokens[index] : nullptr; }

    [[nodiscard]] JsonType TypeOf(int32_t index) const;

    /** @return the value token for key inside the object at objectIndex, or -1. */
    [[nodiscard]] int32_t Find(int32_t objectIndex, const char* key) const;

    [[nodiscard]] const char* GetString(int32_t index, const char* fallback) const;

    [[nodiscard]] bool GetNumber(int32_t index, double& out) const;

    [[nodiscard]] bool GetBool(int32_t index, bool fallback) const;

    /** Re-serializes the subtree at index. */
    void Write(int32_t index, JsonWriter& out) const;

private:
    int32_t ParseValue(Cursor& cursor, int32_t depth);

    bool ParseString(Cursor& cursor, int32_t token);

    Core::Vector<JsonToken> tokens{};
};

/** Streaming JSON writer into an engine-owned byte buffer. The output is not NUL-terminated; use Data() and Size(). */
class JsonWriter
{
public:
    static constexpr size_t MAX_DEPTH = 32;

    JsonWriter() = default;

    JsonWriter(Core::TlsfAllocator* allocator, size_t reserve);

    void BeginObject();

    void BeginArray();

    void End();

    void Key(const char* key);

    void String(const char* value);

    void String(const char* value, size_t length);

    void Number(double value);

    void Int(int64_t value);

    void Bool(bool value);

    void Null();

    /** Appends pre-formatted JSON as one value. */
    void Raw(const char* json, size_t length);

    [[nodiscard]] size_t Depth() const { return stack.Size(); }

    [[nodiscard]] bool InObject() const { return !stack.IsEmpty() && stack.Back().bObject; }

    [[nodiscard]] const char* Data() const { return buffer.Data(); }

    [[nodiscard]] size_t Size() const { return buffer.Size(); }

    void Clear() { buffer.Clear(); stack.Clear(); bAfterKey = false; }

private:
    struct Frame
    {
        bool bObject{false};
        bool bHasElement{false};
    };

    void BeginValue();

    void Put(char c);

    void Put(const char* text, size_t length);

    void PutEscaped(const char* text, size_t length);

    Core::Vector<char> buffer{};
    Core::InlineVector<Frame, MAX_DEPTH> stack{};
    bool bAfterKey{false};
};
} // Engine::MCP

#endif //WILL_ENGINE_MCP_JSON_H
