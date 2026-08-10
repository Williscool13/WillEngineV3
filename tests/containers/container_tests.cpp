//
// Core container test suite. Drafted by Claude.
//
// Focus: the bulk operations that carry trivially-copyable fast paths (memcpy/memset/memmove)
// alongside the placement-new loops for non-trivial types. Every mutation is exercised with BOTH
// a POD element (fast path) and a construction/destruction-counting element (slow path), asserting
// identical observable behavior: element values, ordering, and balanced ctor/dtor counts.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>

#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/arena_vector.h"
#include "core/containers/fixed_vector.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_vector.h"
#include "core/containers/vector.h"
#include "core/memory/arena.h"
#include "core/memory/tlsf_allocator.h"

namespace
{
struct Pod
{
    int32_t a;
    float b;

    bool operator==(const Pod& o) const { return a == o.a && b == o.b; }
};
static_assert(std::is_trivially_copyable_v<Pod> && std::is_trivial_v<Pod>);

struct Tracked
{
    inline static int32_t liveCount = 0;
    inline static int32_t totalConstructions = 0;

    int32_t value{0};

    Tracked() { ++liveCount; ++totalConstructions; }
    explicit Tracked(int32_t v) : value(v) { ++liveCount; ++totalConstructions; }
    Tracked(const Tracked& o) : value(o.value) { ++liveCount; ++totalConstructions; }
    Tracked(Tracked&& o) noexcept : value(o.value) { ++liveCount; ++totalConstructions; }
    Tracked& operator=(const Tracked& o) = default;
    Tracked& operator=(Tracked&& o) noexcept = default;
    ~Tracked() { --liveCount; }

    bool operator==(const Tracked& o) const { return value == o.value; }

    static void ResetCounters()
    {
        liveCount = 0;
        totalConstructions = 0;
    }
};
static_assert(!std::is_trivially_copyable_v<Tracked>);
} // namespace

struct ContainerFixture
{
    static constexpr size_t TLSF_SIZE = 4 * 1024 * 1024;
    static constexpr size_t ARENA_SIZE = 4 * 1024 * 1024;

    std::unique_ptr<char[]> tlsfPool{new char[TLSF_SIZE]};
    std::unique_ptr<char[]> arenaPool{new char[ARENA_SIZE]};

    Core::TlsfAllocator alloc;
    Core::Arena arena;

    ContainerFixture() : arena(arenaPool.get(), ARENA_SIZE, "container-test")
    {
        alloc.Init(tlsfPool.get(), TLSF_SIZE, false, "container-test");
        Tracked::ResetCounters();
    }
};

// ================================================================================
// Vector
// ================================================================================

TEST_CASE_METHOD(ContainerFixture, "Vector: push, growth, and indexed access preserve values", "[containers][vector]")
{
    Core::Vector<Pod> v(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 100; ++i) {
        v.PushBack({i, static_cast<float>(i) * 0.5f});
    }
    REQUIRE(v.Size() == 100);
    for (int32_t i = 0; i < 100; ++i) {
        CHECK(v[i].a == i);
        CHECK(v[i].b == static_cast<float>(i) * 0.5f);
    }
}

TEST_CASE_METHOD(ContainerFixture, "Vector: Resize grow value-initializes new elements", "[containers][vector]")
{
    Core::Vector<Pod> v(&alloc, Core::AllocTag::Unknown);
    v.PushBack({7, 7.0f});
    v.Resize(64);
    REQUIRE(v.Size() == 64);
    CHECK(v[0].a == 7);
    for (size_t i = 1; i < 64; ++i) {
        CHECK(v[i].a == 0);
        CHECK(v[i].b == 0.0f);
    }
}

TEST_CASE_METHOD(ContainerFixture, "Vector: Resize shrink destroys exactly the tail", "[containers][vector]")
{
    Core::Vector<Tracked> v(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 10; ++i) { v.EmplaceBack(i); }
    REQUIRE(Tracked::liveCount == 10);
    v.Resize(4);
    CHECK(v.Size() == 4);
    CHECK(Tracked::liveCount == 4);
    for (int32_t i = 0; i < 4; ++i) { CHECK(v[i].value == i); }
}

TEST_CASE_METHOD(ContainerFixture, "Vector: Append copies a whole range for both element kinds", "[containers][vector]")
{
    Pod src[3] = {{1, 1.0f}, {2, 2.0f}, {3, 3.0f}};
    Core::Vector<Pod> v(&alloc, Core::AllocTag::Unknown);
    v.PushBack({0, 0.0f});
    v.Append(src, src + 3);
    REQUIRE(v.Size() == 4);
    CHECK(v[3].a == 3);

    Core::Vector<Tracked> t(&alloc, Core::AllocTag::Unknown);
    Tracked tsrc[2] = {Tracked{5}, Tracked{6}};
    t.Append(tsrc, tsrc + 2);
    REQUIRE(t.Size() == 2);
    CHECK(t[0].value == 5);
    CHECK(t[1].value == 6);
}

TEST_CASE_METHOD(ContainerFixture, "Vector: Insert in the middle shifts and preserves order", "[containers][vector]")
{
    Core::Vector<int32_t> v(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 5; ++i) { v.PushBack(i); }
    int32_t ins[2] = {90, 91};
    v.Insert(v.Data() + 2, ins, ins + 2);
    REQUIRE(v.Size() == 7);
    const int32_t expected[7] = {0, 1, 90, 91, 2, 3, 4};
    for (size_t i = 0; i < 7; ++i) { CHECK(v[i] == expected[i]); }

    Core::Vector<Tracked> t(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 5; ++i) { t.EmplaceBack(i); }
    Tracked tins[2] = {Tracked{90}, Tracked{91}};
    t.Insert(t.Data() + 2, tins, tins + 2);
    REQUIRE(t.Size() == 7);
    for (size_t i = 0; i < 7; ++i) { CHECK(t[i].value == expected[i]); }
}

TEST_CASE_METHOD(ContainerFixture, "Vector: RemoveAt shifts down and preserves order", "[containers][vector]")
{
    Core::Vector<int32_t> v(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 6; ++i) { v.PushBack(i); }
    v.RemoveAt(2);
    REQUIRE(v.Size() == 5);
    const int32_t expected[5] = {0, 1, 3, 4, 5};
    for (size_t i = 0; i < 5; ++i) { CHECK(v[i] == expected[i]); }

    Core::Vector<Tracked> t(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 6; ++i) { t.EmplaceBack(i); }
    t.RemoveAt(2);
    REQUIRE(t.Size() == 5);
    CHECK(Tracked::liveCount == 5);
    for (size_t i = 0; i < 5; ++i) { CHECK(t[i].value == expected[i]); }
}

TEST_CASE_METHOD(ContainerFixture, "Vector: copy construct and copy assign deep-copy", "[containers][vector]")
{
    Core::Vector<Tracked> a(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 8; ++i) { a.EmplaceBack(i); }

    Core::Vector<Tracked> b(a);
    REQUIRE(b.Size() == 8);
    b[0].value = 999;
    CHECK(a[0].value == 0);

    Core::Vector<Tracked> c(&alloc, Core::AllocTag::Unknown);
    c.EmplaceBack(42);
    c = a;
    REQUIRE(c.Size() == 8);
    CHECK(c[7].value == 7);
    CHECK(Tracked::liveCount == 24);

    Core::Vector<Pod> p(&alloc, Core::AllocTag::Unknown);
    p.PushBack({3, 3.0f});
    Core::Vector<Pod> q(p);
    REQUIRE(q.Size() == 1);
    CHECK(q[0] == p[0]);
}

TEST_CASE_METHOD(ContainerFixture, "Vector: Clear destroys everything exactly once", "[containers][vector]")
{
    Core::Vector<Tracked> v(&alloc, Core::AllocTag::Unknown);
    for (int32_t i = 0; i < 12; ++i) { v.EmplaceBack(i); }
    v.Clear();
    CHECK(v.Size() == 0);
    CHECK(Tracked::liveCount == 0);
}

// ================================================================================
// ArenaVector
// ================================================================================

TEST_CASE_METHOD(ContainerFixture, "ArenaVector: growth relocation preserves values for both element kinds", "[containers][arena-vector]")
{
    Core::ArenaVector<Pod> v(&arena);
    for (int32_t i = 0; i < 100; ++i) { v.PushBack({i, static_cast<float>(i)}); }
    REQUIRE(v.Size() == 100);
    for (int32_t i = 0; i < 100; ++i) { CHECK(v[i].a == i); }

    Core::ArenaVector<Tracked> t(&arena);
    for (int32_t i = 0; i < 100; ++i) { t.EmplaceBack(i); }
    REQUIRE(t.Size() == 100);
    CHECK(Tracked::liveCount == 100);
    for (int32_t i = 0; i < 100; ++i) { CHECK(t[i].value == i); }
}

// Per-frame lights pattern: GatherLights does Clear + Resize every frame and relies on the zero-fill
TEST_CASE_METHOD(ContainerFixture, "ArenaVector: Clear + Resize re-zeroes trivial elements", "[containers][arena-vector]")
{
    Core::ArenaVector<Pod> v(&arena);
    v.Resize(32);
    for (size_t i = 0; i < 32; ++i) { v[i] = {static_cast<int32_t>(i) + 1, 1.0f}; }
    v.Clear();
    v.Resize(32);
    for (size_t i = 0; i < 32; ++i) {
        CHECK(v[i].a == 0);
        CHECK(v[i].b == 0.0f);
    }
}

TEST_CASE_METHOD(ContainerFixture, "ArenaVector: Resize shrink and Insert behave like Vector", "[containers][arena-vector]")
{
    Core::ArenaVector<Tracked> t(&arena);
    for (int32_t i = 0; i < 10; ++i) { t.EmplaceBack(i); }
    t.Resize(6);
    CHECK(Tracked::liveCount == 6);

    Core::ArenaVector<int32_t> v(&arena);
    for (int32_t i = 0; i < 4; ++i) { v.PushBack(i); }
    int32_t ins[1] = {77};
    v.Insert(v.Data() + 1, ins, ins + 1);
    const int32_t expected[5] = {0, 77, 1, 2, 3};
    REQUIRE(v.Size() == 5);
    for (size_t i = 0; i < 5; ++i) { CHECK(v[i] == expected[i]); }
}

TEST_CASE_METHOD(ContainerFixture, "ArenaVector: RemoveAt preserves order for both element kinds", "[containers][arena-vector]")
{
    Core::ArenaVector<int32_t> v(&arena);
    for (int32_t i = 0; i < 6; ++i) { v.PushBack(i); }
    v.RemoveAt(0);
    v.RemoveAt(4);
    const int32_t expected[4] = {1, 2, 3, 4};
    REQUIRE(v.Size() == 4);
    for (size_t i = 0; i < 4; ++i) { CHECK(v[i] == expected[i]); }

    Core::ArenaVector<Tracked> t(&arena);
    for (int32_t i = 0; i < 6; ++i) { t.EmplaceBack(i); }
    t.RemoveAt(3);
    CHECK(Tracked::liveCount == 5);
    CHECK(t[3].value == 4);
}

// ================================================================================
// HeapArray
// ================================================================================

TEST_CASE_METHOD(ContainerFixture, "HeapArray: construction value-initializes all slots", "[containers][heap-array]")
{
    Core::HeapArray<Pod> a(&alloc, Core::AllocTag::Unknown, 64);
    for (size_t i = 0; i < 64; ++i) {
        CHECK(a[i].a == 0);
        CHECK(a[i].b == 0.0f);
    }

    Core::HeapArray<Tracked> t(&alloc, Core::AllocTag::Unknown, 16);
    CHECK(Tracked::liveCount == 16);
    CHECK(t[15].value == 0);
}

TEST_CASE_METHOD(ContainerFixture, "HeapArray: copy construct/assign deep-copy and balance lifetimes", "[containers][heap-array]")
{
    Core::HeapArray<Tracked> a(&alloc, Core::AllocTag::Unknown, 8);
    for (int32_t i = 0; i < 8; ++i) { a[i].value = i; }

    Core::HeapArray<Tracked> b(a);
    REQUIRE(b.Size() == 8);
    CHECK(b[7].value == 7);
    b[0].value = 111;
    CHECK(a[0].value == 0);
    CHECK(Tracked::liveCount == 16);

    b.Reset();
    CHECK(Tracked::liveCount == 8);

    Core::HeapArray<Pod> p(&alloc, Core::AllocTag::Unknown, 4);
    p[2] = {9, 9.0f};
    Core::HeapArray<Pod> q(p);
    CHECK(q[2] == p[2]);
}

// ================================================================================
// FixedVector
// ================================================================================

TEST_CASE_METHOD(ContainerFixture, "FixedVector: push/remove/clear preserve order and lifetimes", "[containers][fixed-vector]")
{
    Core::FixedVector<Tracked> t(&alloc, Core::AllocTag::Unknown, 16);
    for (int32_t i = 0; i < 8; ++i) { t.EmplaceBack(i); }
    t.RemoveAt(2);
    REQUIRE(t.Size() == 7);
    CHECK(Tracked::liveCount == 7);
    const int32_t expected[7] = {0, 1, 3, 4, 5, 6, 7};
    for (size_t i = 0; i < 7; ++i) { CHECK(t[i].value == expected[i]); }
    t.Clear();
    CHECK(Tracked::liveCount == 0);

    Core::FixedVector<Pod> p(&alloc, Core::AllocTag::Unknown, 8);
    p.PushBack({1, 1.0f});
    p.PushBack({2, 2.0f});
    p.PushBack({3, 3.0f});
    p.RemoveAt(0);
    REQUIRE(p.Size() == 2);
    CHECK(p[0].a == 2);
    CHECK(p[1].a == 3);
}

TEST_CASE_METHOD(ContainerFixture, "FixedVector: copy construct deep-copies live prefix only", "[containers][fixed-vector]")
{
    Core::FixedVector<Tracked> a(&alloc, Core::AllocTag::Unknown, 16);
    for (int32_t i = 0; i < 5; ++i) { a.EmplaceBack(i); }
    Core::FixedVector<Tracked> b(a);
    REQUIRE(b.Size() == 5);
    CHECK(b.GetCapacity() == 16);
    CHECK(Tracked::liveCount == 10);
    CHECK(b[4].value == 4);
}

// ================================================================================
// ArenaFixedVector / ArenaArray
// ================================================================================

TEST_CASE_METHOD(ContainerFixture, "ArenaFixedVector: push/remove/copy preserve order and lifetimes", "[containers][arena-fixed-vector]")
{
    Core::ArenaFixedVector<Tracked> t(&arena, 16);
    for (int32_t i = 0; i < 6; ++i) { t.EmplaceBack(i); }
    t.RemoveAt(1);
    REQUIRE(t.Size() == 5);
    CHECK(Tracked::liveCount == 5);
    const int32_t expected[5] = {0, 2, 3, 4, 5};
    for (size_t i = 0; i < 5; ++i) { CHECK(t[i].value == expected[i]); }

    Core::ArenaFixedVector<Tracked> c(t);
    REQUIRE(c.Size() == 5);
    CHECK(c[4].value == 5);
    CHECK(Tracked::liveCount == 10);

    Core::ArenaFixedVector<Pod> p(&arena, 8);
    p.PushBack({4, 4.0f});
    p.PushBack({5, 5.0f});
    p.RemoveAt(0);
    REQUIRE(p.Size() == 1);
    CHECK(p[0].a == 5);
}

TEST_CASE_METHOD(ContainerFixture, "ArenaArray: construction value-initializes, copy deep-copies", "[containers][arena-array]")
{
    Core::ArenaArray<Pod> a(&arena, 32);
    for (size_t i = 0; i < 32; ++i) {
        CHECK(a[i].a == 0);
        CHECK(a[i].b == 0.0f);
    }

    Core::ArenaArray<Tracked> t(&arena, 8);
    CHECK(Tracked::liveCount == 8);
    t[3].value = 33;
    Core::ArenaArray<Tracked> u(t);
    CHECK(u[3].value == 33);
    CHECK(Tracked::liveCount == 16);
    u.Clear();
    CHECK(Tracked::liveCount == 8);
}

// ================================================================================
// InlineVector
// ================================================================================

TEST_CASE("InlineVector: RemoveAt shifts down for both element kinds", "[containers][inline-vector]")
{
    Core::InlineVector<int32_t, 8> v{0, 1, 2, 3, 4, 5};
    v.RemoveAt(2);
    REQUIRE(v.Size() == 5);
    const int32_t expected[5] = {0, 1, 3, 4, 5};
    for (size_t i = 0; i < 5; ++i) { CHECK(v[i] == expected[i]); }
    v.RemoveAt(4);
    CHECK(v.Size() == 4);
    CHECK(v[3] == 4);

    Tracked::ResetCounters();
    Core::InlineVector<Tracked, 8> t;
    for (int32_t i = 0; i < 5; ++i) { t.PushBack(Tracked{i}); }
    t.RemoveAt(0);
    REQUIRE(t.Size() == 4);
    for (int32_t i = 0; i < 4; ++i) { CHECK(t[i].value == i + 1); }
}
