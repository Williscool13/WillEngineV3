//
// StringID test suite. Drafted by Claude.
//
// StringID is serialized into assets; its hash must NEVER change. The XXH3 known-answer vectors in hash_tests.cpp are the tripwire for that; this file covers the StringID API.

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "core/hash/xxh3.h"
#include "core/string_id.h"

TEST_CASE("Runtime StringID and _sid literal produce the same id", "[stringid]")
{
    const char* runtime = "gbuffer";
    const StringID a{runtime, strlen(runtime)};
    const StringID b = "gbuffer"_sid;
    CHECK(a == b);
    CHECK(a.id == Hash("gbuffer", 7));
}

TEST_CASE("StringID validity, equality, and ordering", "[stringid]")
{
    const StringID invalid{};
    CHECK_FALSE(invalid.IsValid());
    CHECK_FALSE(static_cast<bool>(invalid));
    CHECK(invalid == StringID::Invalid);

    const StringID a = "alpha"_sid;
    const StringID b = "beta"_sid;
    CHECK(a.IsValid());
    CHECK(a != b);
    CHECK((a < b) == (a.id < b.id));
    CHECK(std::hash<StringID>{}(a) == static_cast<size_t>(a.id));
}

#ifdef WDEBUG
TEST_CASE("Debug interning resolves a runtime StringID back to its string", "[stringid]")
{
    const char* token = "stringid_test_unique_token";
    const StringID sid{token, strlen(token)};
    CHECK(strcmp(sid.ToString(), "stringid_test_unique_token") == 0);
    CHECK(strcmp(StringID{0xDEADBEEFDEADBEEFULL}.ToString(), "unknown") == 0);
}
#endif
