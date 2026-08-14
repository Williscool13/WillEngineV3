//
// Asset text format test suite. Drafted by Claude.
//
// Locks the TextWriter/TextReader pair for the bespoke `key|value` body format
// (docs/serialization/text_format.md) plus the hex-float header fields in
// .wsfont/.wprobe. Byte-identical re-serialization is load-bearing: the scene
// prefab-diff compares serialized fragments by bytes.

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <string_view>

#include "core/memory/tlsf_allocator.h"
#include "engine/resources/environment_map/probe_format.h"
#include "engine/resources/font/font_format.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

struct TextFormatFixture
{
    static constexpr size_t TLSF_SIZE = 4 * 1024 * 1024;

    std::unique_ptr<char[]> tlsfPool{new char[TLSF_SIZE]};
    Core::TlsfAllocator alloc;

    TextFormatFixture()
    {
        alloc.Init(tlsfPool.get(), TLSF_SIZE, false, "text-format-test");
    }

    static std::string_view View(const Core::Vector<std::byte>& buf)
    {
        return {reinterpret_cast<const char*>(buf.Data()), buf.Size()};
    }

    static Engine::TextReader Reader(const Core::Vector<std::byte>& buf)
    {
        return {buf.Data(), buf.Size()};
    }

    static Engine::TextReader Reader(std::string_view text)
    {
        return {text.data(), text.data() + text.size()};
    }
};

TEST_CASE_METHOD(TextFormatFixture, "TextWriter: canonical field encodings", "[textformat]")
{
    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w(buf);

    w.Key("mass", 5.0f);
    w.Key("translation", glm::vec3(-20.0f, 6.0f, -20.0f));
    w.Key("rotation", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    w.Key("isSensor", true);
    w.Key("fontId", static_cast<uint64_t>(11302268835193496650ull));
    w.Key("layer", static_cast<int32_t>(-3));
    w.KeyStr("name", "[Bleed] Floor");

    constexpr const char* EXPECTED =
        "mass|0x40a00000\n"
        "translation|0xc1a00000|0x40c00000|0xc1a00000\n"
        "rotation|0x3f800000|0x00000000|0x00000000|0x00000000\n"
        "isSensor|1\n"
        "fontId|11302268835193496650\n"
        "layer|-3\n"
        "name|[Bleed] Floor\n";
    CHECK(View(buf) == EXPECTED);
}

TEST_CASE_METHOD(TextFormatFixture, "TextReader: round-trip preserves every value and re-serializes byte-identical", "[textformat]")
{
    Core::Vector<std::byte> a(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter wa(a);
    wa.Key("mass", 3.14159265f);
    wa.Key("count", 42u);
    wa.Key("id", static_cast<uint64_t>(12304496605836952073ull));
    wa.Key("offset", glm::vec3(0.1f, -0.2f, 1000000.5f));
    wa.Key("tint", glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
    wa.Key("rot", glm::quat(0.7071068f, 0.0f, 0.7071068f, 0.0f));
    wa.Key("indices", glm::ivec4(-1, 0, 7, 255));
    wa.Key("enabled", false);
    wa.KeyStr("label", "hello");

    const Engine::TextReader r = Reader(a);
    CHECK(Engine::FloatBits(r.Float("mass")) == Engine::FloatBits(3.14159265f));
    CHECK(r.UInt("count") == 42u);
    CHECK(r.U64("id") == 12304496605836952073ull);
    const glm::vec3 off = r.Vec3("offset");
    CHECK(Engine::FloatBits(off.y) == Engine::FloatBits(-0.2f));
    const glm::quat q = r.Quat("rot");
    CHECK(Engine::FloatBits(q.w) == Engine::FloatBits(0.7071068f));
    CHECK(Engine::FloatBits(q.y) == Engine::FloatBits(0.7071068f));
    CHECK(r.IVec4("indices") == glm::ivec4(-1, 0, 7, 255));
    CHECK_FALSE(r.Bool("enabled", true));
    Core::InlineString<64> label;
    REQUIRE(r.Str("label", label));
    CHECK(label == "hello");

    Core::Vector<std::byte> b(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter wb(b);
    wb.Key("mass", r.Float("mass"));
    wb.Key("count", r.UInt("count"));
    wb.Key("id", r.U64("id"));
    wb.Key("offset", r.Vec3("offset"));
    wb.Key("tint", r.Vec4("tint"));
    wb.Key("rot", r.Quat("rot"));
    wb.Key("indices", r.IVec4("indices"));
    wb.Key("enabled", r.Bool("enabled"));
    wb.KeyStr("label", label.View());
    CHECK(View(a) == View(b));
}

TEST_CASE_METHOD(TextFormatFixture, "Hex floats: pathological bit patterns round-trip exactly", "[textformat]")
{
    const uint32_t PATTERNS[] = {
        0x00000000u, // +0
        0x80000000u, // -0
        0x00000001u, // smallest denormal
        0x7f800000u, // +inf
        0xff800000u, // -inf
        0x7fc00000u, // quiet NaN
        0x3f800001u, // 1.0 + 1ulp
    };
    for (const uint32_t bits : PATTERNS) {
        Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
        Engine::TextWriter w(buf);
        w.Key("v", std::bit_cast<float>(bits));
        CHECK(Engine::FloatBits(Reader(buf).Float("v")) == bits);
    }
}

TEST_CASE_METHOD(TextFormatFixture, "Omit-default: absent keys restore defaults, all-default emits nothing", "[textformat]")
{
    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w(buf);
    w.KeyOpt("scale", glm::vec3(1.0f), glm::vec3(1.0f));
    w.KeyOpt("mass", 1.0f, 1.0f);
    w.KeyOpt("enabled", true, true);
    CHECK(buf.Size() == 0);

    w.KeyOpt("mass", 2.5f, 1.0f);
    CHECK(View(buf) == "mass|0x40200000\n");

    const Engine::TextReader r = Reader(buf);
    CHECK_FALSE(r.Has("scale"));
    CHECK(r.Vec3("scale", glm::vec3(1.0f)) == glm::vec3(1.0f));
    CHECK(Engine::FloatBits(r.Float("mass", 1.0f)) == Engine::FloatBits(2.5f));
    CHECK(r.Bool("enabled", true));
}

TEST_CASE_METHOD(TextFormatFixture, "Strings: escaping, embedded pipes, and lines past 256 bytes", "[textformat]")
{
    Core::InlineString<512> original;
    for (int32_t i = 0; i < 300; ++i) {
        original.Append("x", 1);
    }
    original.Append("|pipe\\slash\nnewline\rcr", 22);

    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w(buf);
    w.KeyStr("text", original.View());
    w.Key("after", 7u);

    const Engine::TextReader r = Reader(buf);
    Core::InlineString<512> decoded;
    REQUIRE(r.Str("text", decoded));
    CHECK(decoded == original);
    CHECK(r.UInt("after") == 7u);

    Core::InlineString<16> empty("nonempty");
    Core::Vector<std::byte> buf2(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w2(buf2);
    w2.KeyStr("text", "");
    REQUIRE(Reader(buf2).Str("text", empty));
    CHECK(empty.IsEmpty());
}

TEST_CASE_METHOD(TextFormatFixture, "Blocks: nesting, arrays, and top-level key isolation", "[textformat]")
{
    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w(buf);
    w.Key("motionType", 1u);
    w.Count("shapes", 2);
    w.BeginBlock("shape");
    w.Key("type", 0u);
    w.Key("halfExtents", glm::vec3(0.5f));
    w.BeginBlock("splineParams");
    w.Key("radius", 0.4f);
    w.EndBlock();
    w.EndBlock();
    w.BeginBlock("shape");
    w.Key("type", 1u);
    w.Key("radius", 0.5f);
    w.EndBlock();
    w.Key("mass", 9.0f);

    const Engine::TextReader r = Reader(buf);
    CHECK(r.UInt("motionType") == 1u);
    CHECK(Engine::FloatBits(r.Float("mass")) == Engine::FloatBits(9.0f));
    // "radius" and "type" live inside records, never at this level
    CHECK_FALSE(r.Has("radius"));
    CHECK_FALSE(r.Has("type"));
    CHECK(r.RecordCount("shapes") == 2u);

    int32_t seen = 0;
    r.ForEachRecord("shapes", [&](const Engine::TextReader& shape) {
        if (seen == 0) {
            CHECK(shape.UInt("type") == 0u);
            CHECK(shape.Vec3("halfExtents") == glm::vec3(0.5f));
            REQUIRE(shape.HasBlock("splineParams"));
            CHECK(Engine::FloatBits(shape.Block("splineParams").Float("radius")) == Engine::FloatBits(0.4f));
            CHECK_FALSE(shape.Has("radius"));
        }
        else {
            CHECK(shape.UInt("type") == 1u);
            CHECK(Engine::FloatBits(shape.Float("radius")) == Engine::FloatBits(0.5f));
        }
        ++seen;
    });
    CHECK(seen == 2);
}

TEST_CASE_METHOD(TextFormatFixture, "Compat: unknown fields and unknown blocks are skipped", "[textformat]")
{
    constexpr std::string_view TEXT =
        "known|5\n"
        "futureField|0x40a00000|extra|stuff\n"
        "futureBlock\n"
        "known|999\n"
        "deeper\n"
        "known|888\n"
        ";\n"
        ";\n"
        "after|6\n";
    const Engine::TextReader r = Reader(TEXT);
    CHECK(r.UInt("known") == 5u);
    CHECK(r.UInt("after") == 6u);
    CHECK_FALSE(r.HasBlock("deeper"));
    CHECK(r.HasBlock("futureBlock"));
}

TEST_CASE_METHOD(TextFormatFixture, "ForEachBlock: scene-style entity/component iteration", "[textformat]")
{
    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    Engine::TextWriter w(buf);
    w.BeginBlock("entity");
    w.BeginBlock(static_cast<uint64_t>(1884906092559237059ull));
    w.Key("translation", glm::vec3(5.0f, 1.0f, -3.0f));
    w.EndBlock();
    w.BeginBlock(static_cast<uint64_t>(3048324656540157766ull));
    w.KeyStr("name", "Floor");
    w.EndBlock();
    w.EndBlock();
    w.BeginBlock("entity");
    w.EndBlock();

    int32_t entityCount = 0;
    Reader(buf).ForEachBlock([&](std::string_view opener, const Engine::TextReader& entity) {
        CHECK(opener == "entity");
        if (entityCount == 0) {
            int32_t componentCount = 0;
            entity.ForEachBlock([&](std::string_view typeId, const Engine::TextReader& comp) {
                uint64_t id = 0;
                std::from_chars(typeId.data(), typeId.data() + typeId.size(), id);
                if (id == 1884906092559237059ull) {
                    CHECK(comp.Vec3("translation") == glm::vec3(5.0f, 1.0f, -3.0f));
                }
                else {
                    CHECK(id == 3048324656540157766ull);
                }
                ++componentCount;
            });
            CHECK(componentCount == 2);
        }
        ++entityCount;
    });
    CHECK(entityCount == 2);
}

TEST_CASE_METHOD(TextFormatFixture, "Font header: hex-float metrics round-trip bit-exactly, version 1 rejected", "[textformat]")
{
    Engine::WFontHeader h{};
    strcpy_s(h.name, "roboto");
    h.fontId = 11302268835193496650ull;
    h.emSize = 1.0f;
    h.ascender = 0.9277344f;
    h.descender = -0.24414062f;
    h.lineHeight = 1.171875f;
    h.glyphCount = 95;
    h.atlasWidth = 512;
    h.atlasHeight = 512;

    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    REQUIRE(Engine::WriteWFontHeader(buf, h));

    const auto read = Engine::ReadWFontHeader(buf.Data(), buf.Size());
    REQUIRE(read.has_value());
    CHECK(Engine::FloatBits(read->emSize) == Engine::FloatBits(h.emSize));
    CHECK(Engine::FloatBits(read->ascender) == Engine::FloatBits(h.ascender));
    CHECK(Engine::FloatBits(read->descender) == Engine::FloatBits(h.descender));
    CHECK(Engine::FloatBits(read->lineHeight) == Engine::FloatBits(h.lineHeight));
    CHECK(read->glyphCount == 95u);

    constexpr std::string_view OLD =
        "wsfont\n"
        "version 1 0\n"
        "em_size 1\n"
        "atlas_compression 0\n"
        "end_header\n";
    CHECK_FALSE(Engine::ReadWFontHeader(OLD.data(), OLD.size()).has_value());
}

TEST_CASE_METHOD(TextFormatFixture, "Probe header: hex-float snapshot round-trips bit-exactly", "[textformat]")
{
    Engine::WProbeHeader h{};
    strcpy_s(h.name, "lab_probe");
    h.probeId = 42;
    h.snapshot.translation[0] = 0.1f;
    h.snapshot.translation[1] = -0.0f;
    h.snapshot.translation[2] = std::bit_cast<float>(0x00000001u);
    h.snapshot.rotation[1] = 0.7071068f;
    h.snapshot.scale[0] = 2.5f;
    h.snapshot.captureOffset[2] = -3.25f;

    Core::Vector<std::byte> buf(&alloc, Core::AllocTag::Unknown);
    REQUIRE(Engine::WriteWProbeHeader(buf, h));

    const auto read = Engine::ReadWProbeHeader(buf.Data(), buf.Size());
    REQUIRE(read.has_value());
    for (int32_t i = 0; i < 3; ++i) {
        CHECK(Engine::FloatBits(read->snapshot.translation[i]) == Engine::FloatBits(h.snapshot.translation[i]));
        CHECK(Engine::FloatBits(read->snapshot.scale[i]) == Engine::FloatBits(h.snapshot.scale[i]));
        CHECK(Engine::FloatBits(read->snapshot.captureOffset[i]) == Engine::FloatBits(h.snapshot.captureOffset[i]));
    }
    for (int32_t i = 0; i < 4; ++i) {
        CHECK(Engine::FloatBits(read->snapshot.rotation[i]) == Engine::FloatBits(h.snapshot.rotation[i]));
    }
}
