//
// StringID test suite. Drafted by Claude.
//
// StringID is serialized into assets; its hash must NEVER change. The XXH3 known-answer vectors in hash_tests.cpp are the tripwire for that; this file covers the StringID API.

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "core/hash/xxh3.h"
#include "core/string_id.h"

TEST_CASE("SID, runtime StringID, and _sid literal produce the same id", "[stringid]")
{
    const StringID a = SID("gbuffer");
    const char* runtime = "gbuffer";
    const StringID b{runtime, strlen(runtime)};
    const StringID c = "gbuffer"_sid;
    CHECK(a == b);
    CHECK(b == c);
    CHECK(a.id == Hash("gbuffer", 7));
}

TEST_CASE("StringID validity, equality, and ordering", "[stringid]")
{
    const StringID invalid{};
    CHECK_FALSE(invalid.IsValid());
    CHECK_FALSE(static_cast<bool>(invalid));
    CHECK(invalid == StringID::Invalid);

    const StringID a = SID("alpha");
    const StringID b = SID("beta");
    CHECK(a.IsValid());
    CHECK(a != b);
    CHECK((a < b) == (a.id < b.id));
    CHECK(std::hash<StringID>{}(a) == static_cast<size_t>(a.id));
}

TEST_CASE("MakeConcatStringId equals hashing the concatenation", "[stringid]")
{
    const StringID concat = MakeConcatStringId("depth_", 6, "prepass", 7);
    CHECK(concat == SID("depth_prepass"));

    const Core::InlineString<> prefix{"hiz_"};
    CHECK(SID_CONCAT(prefix, "build") == SID("hiz_build"));
}

TEST_CASE("MakeConcatStringId truncates at its 255-char buffer", "[stringid]")
{
    char longA[300];
    memset(longA, 'a', sizeof(longA));
    const StringID truncated = MakeConcatStringId(longA, 300, "b", 1);
    CHECK(truncated == StringID{longA, 255});
}

#ifdef WDEBUG
TEST_CASE("Debug interning resolves a SID back to its string", "[stringid]")
{
    const StringID sid = SID("stringid_test_unique_token");
    CHECK(strcmp(sid.ToString(), "stringid_test_unique_token") == 0);
    CHECK(strcmp(StringID{0xDEADBEEFDEADBEEFULL}.ToString(), "unknown") == 0);
}
#endif
