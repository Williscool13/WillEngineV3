//
// In-house XXH3 versus the reference xxHash (extern/xxhash, test oracle only).
// Hash() output is serialized into assets and must stay bit-identical to XXH3_64bits.
//

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "core/hash/xxh3.h"
#include "core/string_id.h"

static std::vector<uint8_t> PatternBuffer(size_t size)
{
    std::vector<uint8_t> buf(size);
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < size; ++i) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = static_cast<uint8_t>(s >> 56);
    }
    return buf;
}

TEST_CASE("Hash matches reference XXH3_64bits for every length 0..5000", "[hash]")
{
    const std::vector<uint8_t> buf = PatternBuffer(5000);
    for (size_t len = 0; len <= buf.size(); ++len) {
        const uint64_t ref = XXH3_64bits(buf.data(), len);
        REQUIRE(Hash(buf.data(), len) == ref);
        REQUIRE(Hash(static_cast<const void*>(buf.data()), len) == ref);
        REQUIRE(Hash(reinterpret_cast<const char*>(buf.data()), len) == ref);
    }
}

TEST_CASE("Hash folds at compile time on every length path", "[hash]")
{
    static constexpr char LONG[] = "The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog!";
    constexpr uint64_t empty = Hash("", 0);
    constexpr uint64_t one = Hash("a", 1);
    constexpr uint64_t five = Hash("white", 5);
    constexpr uint64_t eighteen = Hash("TransformComponent", 18);
    constexpr uint64_t mid = Hash("The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog!", 89);
    constexpr uint64_t mid2 = Hash(LONG, 200);
    constexpr uint64_t longPath = Hash(LONG, sizeof(LONG) - 1);
    CHECK(empty == XXH3_64bits("", 0));
    CHECK(one == XXH3_64bits("a", 1));
    CHECK(five == XXH3_64bits("white", 5));
    CHECK(eighteen == XXH3_64bits("TransformComponent", 18));
    CHECK(mid == XXH3_64bits("The quick brown fox jumps over the lazy dog. The quick brown fox jumps over the lazy dog!", 89));
    CHECK(mid2 == XXH3_64bits(LONG, 200));
    CHECK(longPath == XXH3_64bits(LONG, sizeof(LONG) - 1));
}

// Same pins as scripts/xxh3.py VECTORS: the Python authoring scripts must mint the same ids the engine recomputes.
TEST_CASE("Hash matches the pinned cross-language vectors", "[hash]")
{
    CHECK(Hash("", 0) == 3244421341483603138ULL);
    CHECK(Hash("a", 1) == 16629034431890738719ULL);
    CHECK(Hash("abc", 3) == 8696274497037089104ULL);
    CHECK(Hash("TransformComponent", 18) == 8357514868041474787ULL);
    CHECK(Hash("default_lit", 11) == 16532098932897623660ULL);
    CHECK(Hash("default_pbr", 11) == 14720002576866434405ULL);
    CHECK(Hash("default_pbr_restir", 18) == 6423489698308471953ULL);
    CHECK(Hash("Jump", 4) == 8077407883072430183ULL);
    CHECK(SID("gbuffer").id == 17000617961446832639ULL);
}

TEST_CASE("HashBuilder is deterministic and order-sensitive", "[hash]")
{
    const std::vector<uint8_t> buf = PatternBuffer(64);
    HashBuilder a;
    a.Add(buf.data(), 10);
    a.Add(3.5f);
    a.Add(static_cast<uint8_t>(7));
    HashBuilder b;
    b.Add(buf.data(), 10);
    b.Add(3.5f);
    b.Add(static_cast<uint8_t>(7));
    CHECK(a.Finish() == b.Finish());

    HashBuilder c;
    c.Add(3.5f);
    c.Add(buf.data(), 10);
    c.Add(static_cast<uint8_t>(7));
    CHECK(a.Finish() != c.Finish());

    HashBuilder d;
    d.Add(buf.data(), 10);
    CHECK(d.Finish() != Hash(buf.data(), 10));
}
