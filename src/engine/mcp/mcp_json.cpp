//
// Created by William on 2026-09-05.
//

#include "mcp_json.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/containers/inline_string.h"
#include "core/memory/tlsf_allocator.h"

namespace Engine::MCP
{
static constexpr size_t TOKEN_RESERVE = 64;
static constexpr size_t NUMBER_TEXT_MAX = 64;

JsonReader::JsonReader(Core::TlsfAllocator* allocator)
    : tokens(allocator, Core::AllocTag::MCPServer, TOKEN_RESERVE) {}

static void SkipWhitespace(JsonReader::Cursor& c)
{
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) { ++c.p; }
}

static bool Match(JsonReader::Cursor& c, const char* literal)
{
    const size_t n = strlen(literal);
    if (static_cast<size_t>(c.end - c.p) < n || memcmp(c.p, literal, n) != 0) { return false; }
    c.p += n;
    return true;
}

static int HexValue(const char ch)
{
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

static bool ReadHex4(const char* p, const char* end, uint32_t& out)
{
    if (end - p < 4) { return false; }
    out = 0;
    for (int i = 0; i < 4; ++i) {
        const int v = HexValue(p[i]);
        if (v < 0) { return false; }
        out = (out << 4) | static_cast<uint32_t>(v);
    }
    return true;
}

static char* PutUtf8(char* out, uint32_t cp)
{
    if (cp < 0x80) {
        *out++ = static_cast<char>(cp);
    }
    else if (cp < 0x800) {
        *out++ = static_cast<char>(0xC0 | (cp >> 6));
        *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000) {
        *out++ = static_cast<char>(0xE0 | (cp >> 12));
        *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    }
    else {
        *out++ = static_cast<char>(0xF0 | (cp >> 18));
        *out++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        *out++ = static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

bool JsonReader::ParseString(Cursor& c, const int32_t token)
{
    ++c.p;
    char* out = c.p;
    char* start = c.p;
    while (c.p < c.end) {
        const char ch = *c.p;
        if (ch == '"') {
            *out = '\0';
            tokens[token].type = JsonType::String;
            tokens[token].text = start;
            tokens[token].length = static_cast<uint32_t>(out - start);
            ++c.p;
            return true;
        }
        if (static_cast<unsigned char>(ch) < 0x20) { return false; }
        if (ch != '\\') {
            *out++ = ch;
            ++c.p;
            continue;
        }
        ++c.p;
        if (c.p >= c.end) { return false; }
        const char esc = *c.p++;
        switch (esc) {
            case '"': *out++ = '"'; break;
            case '\\': *out++ = '\\'; break;
            case '/': *out++ = '/'; break;
            case 'b': *out++ = '\b'; break;
            case 'f': *out++ = '\f'; break;
            case 'n': *out++ = '\n'; break;
            case 'r': *out++ = '\r'; break;
            case 't': *out++ = '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!ReadHex4(c.p, c.end, cp)) { return false; }
                c.p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low = 0;
                    if (c.end - c.p < 6 || c.p[0] != '\\' || c.p[1] != 'u' || !ReadHex4(c.p + 2, c.end, low) || low < 0xDC00 || low > 0xDFFF) { return false; }
                    c.p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                out = PutUtf8(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

int32_t JsonReader::ParseValue(Cursor& c, const int32_t depth)
{
    if (depth > MAX_DEPTH) { return -1; }
    SkipWhitespace(c);
    if (c.p >= c.end) { return -1; }

    const int32_t index = static_cast<int32_t>(tokens.Size());
    tokens.PushBack(JsonToken{});

    const char ch = *c.p;
    if (ch == '{' || ch == '[') {
        const bool bObject = ch == '{';
        const char close = bObject ? '}' : ']';
        tokens[index].type = bObject ? JsonType::Object : JsonType::Array;
        ++c.p;
        int32_t last = -1;
        while (true) {
            SkipWhitespace(c);
            if (c.p >= c.end) { return -1; }
            if (*c.p == close) {
                ++c.p;
                return index;
            }
            if (last >= 0) {
                if (*c.p != ',') { return -1; }
                ++c.p;
                SkipWhitespace(c);
            }
            if (bObject) {
                if (c.p >= c.end || *c.p != '"') { return -1; }
                const int32_t key = static_cast<int32_t>(tokens.Size());
                tokens.PushBack(JsonToken{});
                if (!ParseString(c, key)) { return -1; }
                SkipWhitespace(c);
                if (c.p >= c.end || *c.p != ':') { return -1; }
                ++c.p;
                const int32_t value = ParseValue(c, depth + 1);
                if (value < 0) { return -1; }
                tokens[key].next = value;
                if (last < 0) { tokens[index].firstChild = key; } else { tokens[last].next = key; }
                last = value;
            }
            else {
                const int32_t value = ParseValue(c, depth + 1);
                if (value < 0) { return -1; }
                if (last < 0) { tokens[index].firstChild = value; } else { tokens[last].next = value; }
                last = value;
            }
            tokens[index].childCount++;
        }
    }
    if (ch == '"') {
        return ParseString(c, index) ? index : -1;
    }
    if (Match(c, "true")) {
        tokens[index].type = JsonType::Bool;
        tokens[index].text = "true";
        tokens[index].length = 4;
        return index;
    }
    if (Match(c, "false")) {
        tokens[index].type = JsonType::Bool;
        tokens[index].text = "false";
        tokens[index].length = 5;
        return index;
    }
    if (Match(c, "null")) {
        tokens[index].type = JsonType::Null;
        return index;
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        const char* start = c.p;
        while (c.p < c.end && (*c.p == '-' || *c.p == '+' || *c.p == '.' || *c.p == 'e' || *c.p == 'E' || (*c.p >= '0' && *c.p <= '9'))) { ++c.p; }
        tokens[index].type = JsonType::Number;
        tokens[index].text = start;
        tokens[index].length = static_cast<uint32_t>(c.p - start);
        return index;
    }
    return -1;
}

bool JsonReader::Parse(char* text, const size_t length)
{
    tokens.Clear();
    Cursor c{text, text + length};
    if (ParseValue(c, 0) != 0) {
        tokens.Clear();
        return false;
    }
    SkipWhitespace(c);
    if (c.p != c.end) {
        tokens.Clear();
        return false;
    }
    return true;
}

JsonType JsonReader::TypeOf(const int32_t index) const
{
    const JsonToken* t = Get(index);
    return t ? t->type : JsonType::Invalid;
}

int32_t JsonReader::Find(const int32_t objectIndex, const char* key) const
{
    const JsonToken* object = Get(objectIndex);
    if (!object || object->type != JsonType::Object) { return -1; }
    int32_t k = object->firstChild;
    while (k >= 0) {
        const JsonToken& keyToken = tokens[k];
        if (strcmp(keyToken.text, key) == 0) { return keyToken.next; }
        k = tokens[keyToken.next].next;
    }
    return -1;
}

const char* JsonReader::GetString(const int32_t index, const char* fallback) const
{
    const JsonToken* t = Get(index);
    return t && t->type == JsonType::String ? t->text : fallback;
}

bool JsonReader::GetNumber(const int32_t index, double& out) const
{
    const JsonToken* t = Get(index);
    if (!t || t->type != JsonType::Number || t->length >= NUMBER_TEXT_MAX) { return false; }
    char buf[NUMBER_TEXT_MAX];
    memcpy(buf, t->text, t->length);
    buf[t->length] = '\0';
    char* end = nullptr;
    out = strtod(buf, &end);
    return end == buf + t->length;
}

bool JsonReader::GetBool(const int32_t index, const bool fallback) const
{
    const JsonToken* t = Get(index);
    return t && t->type == JsonType::Bool ? t->text[0] == 't' : fallback;
}

void JsonReader::Write(const int32_t index, JsonWriter& out) const
{
    const JsonToken* t = Get(index);
    if (!t) { return; }
    switch (t->type) {
        case JsonType::Null: out.Null(); break;
        case JsonType::Bool: out.Bool(t->text[0] == 't'); break;
        case JsonType::Number: out.Raw(t->text, t->length); break;
        case JsonType::String: out.String(t->text, t->length); break;
        case JsonType::Array: {
            out.BeginArray();
            for (int32_t child = t->firstChild; child >= 0; child = tokens[child].next) { Write(child, out); }
            out.End();
            break;
        }
        case JsonType::Object: {
            out.BeginObject();
            for (int32_t key = t->firstChild; key >= 0;) {
                const int32_t value = tokens[key].next;
                out.Key(tokens[key].text);
                Write(value, out);
                key = tokens[value].next;
            }
            out.End();
            break;
        }
        case JsonType::Invalid: break;
    }
}

JsonWriter::JsonWriter(Core::TlsfAllocator* allocator, const size_t reserve)
    : buffer(allocator, Core::AllocTag::MCPServer, reserve) {}

void JsonWriter::Put(const char c)
{
    buffer.PushBack(c);
}

void JsonWriter::Put(const char* text, const size_t length)
{
    buffer.Append(text, text + length);
}

void JsonWriter::PutEscaped(const char* text, const size_t length)
{
    static constexpr char HEX[] = "0123456789abcdef";
    Put('"');
    for (size_t i = 0; i < length; ++i) {
        const char ch = text[i];
        switch (ch) {
            case '"': Put("\\\"", 2); break;
            case '\\': Put("\\\\", 2); break;
            case '\n': Put("\\n", 2); break;
            case '\r': Put("\\r", 2); break;
            case '\t': Put("\\t", 2); break;
            case '\b': Put("\\b", 2); break;
            case '\f': Put("\\f", 2); break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    const char seq[6] = {'\\', 'u', '0', '0', HEX[(ch >> 4) & 0xF], HEX[ch & 0xF]};
                    Put(seq, 6);
                }
                else {
                    Put(ch);
                }
                break;
        }
    }
    Put('"');
}

void JsonWriter::BeginValue()
{
    if (bAfterKey) {
        bAfterKey = false;
        return;
    }
    if (!stack.IsEmpty()) {
        assert(!stack.Back().bObject && "JsonWriter: value inside an object needs a Key first");
        if (stack.Back().bHasElement) { Put(','); }
        stack.Back().bHasElement = true;
    }
}

void JsonWriter::BeginObject()
{
    BeginValue();
    assert(!stack.IsFull());
    stack.PushBack(Frame{true, false});
    Put('{');
}

void JsonWriter::BeginArray()
{
    BeginValue();
    assert(!stack.IsFull());
    stack.PushBack(Frame{false, false});
    Put('[');
}

void JsonWriter::End()
{
    assert(!stack.IsEmpty() && !bAfterKey);
    Put(stack.Back().bObject ? '}' : ']');
    stack.PopBack();
}

void JsonWriter::Key(const char* key)
{
    assert(InObject() && !bAfterKey);
    if (stack.Back().bHasElement) { Put(','); }
    stack.Back().bHasElement = true;
    PutEscaped(key, strlen(key));
    Put(':');
    bAfterKey = true;
}

void JsonWriter::String(const char* value)
{
    String(value, strlen(value));
}

void JsonWriter::String(const char* value, const size_t length)
{
    BeginValue();
    PutEscaped(value, length);
}

void JsonWriter::Number(const double value)
{
    if (std::isnan(value) || std::isinf(value)) {
        Null();
        return;
    }
    BeginValue();
    const auto text = Core::InlineString<32>::Format("%.15g", value);
    Put(text.c_str(), text.Size());
}

void JsonWriter::Int(const int64_t value)
{
    BeginValue();
    const auto text = Core::InlineString<32>::Format("%lld", static_cast<long long>(value));
    Put(text.c_str(), text.Size());
}

void JsonWriter::Bool(const bool value)
{
    BeginValue();
    if (value) { Put("true", 4); } else { Put("false", 5); }
}

void JsonWriter::Null()
{
    BeginValue();
    Put("null", 4);
}

void JsonWriter::Raw(const char* json, const size_t length)
{
    BeginValue();
    Put(json, length);
}
} // Engine::MCP
