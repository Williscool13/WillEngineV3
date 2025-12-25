//
// Created by William on 2025-12-24.
//

#include <map>
#include <catch2/catch_test_macros.hpp>
#include "core/allocators/handle_allocator.h"

using namespace Core;

struct DummyType {};

TEST_CASE("HandleAllocator allocation and deallocation", "[data-structures][handle-allocator]") {
    HandleAllocator<DummyType, 8> allocator;

    SECTION("Starts with capacity available") {
        REQUIRE(allocator.IsAnyFree());
        REQUIRE(allocator.GetCount() == 0);
        REQUIRE(allocator.GetCapacity() == 8);
    }

    SECTION("Add returns valid handle") {
        auto handle = allocator.Add();
        REQUIRE(handle.IsValid());
        REQUIRE(allocator.IsValid(handle));
        REQUIRE(allocator.GetCount() == 1);
    }

    SECTION("IsValid with invalid handle returns false") {
        REQUIRE_FALSE(allocator.IsValid(Handle<DummyType>::INVALID));
    }

    SECTION("Fills to capacity") {
        std::vector<Handle<DummyType>> handles;
        for (int i = 0; i < 8; i++) {
            auto h = allocator.Add();
            REQUIRE(h.IsValid());
            handles.push_back(h);
        }

        REQUIRE_FALSE(allocator.IsAnyFree());
        REQUIRE(allocator.GetCount() == 8);

        auto overflow = allocator.Add();
        REQUIRE_FALSE(overflow.IsValid());
    }

    SECTION("Remove frees slot for reuse") {
        auto h1 = allocator.Add();
        REQUIRE(allocator.Remove(h1));
        REQUIRE(allocator.IsAnyFree());
        REQUIRE(allocator.GetCount() == 0);

        auto h2 = allocator.Add();
        REQUIRE(h2.IsValid());
        REQUIRE(allocator.GetCount() == 1);
    }

    SECTION("Remove with invalid handle returns false") {
        REQUIRE_FALSE(allocator.Remove(Handle<DummyType>::INVALID));
    }

    SECTION("Clear empties all slots") {
        allocator.Add();
        allocator.Add();

        allocator.Clear();
        REQUIRE(allocator.IsAnyFree());
        REQUIRE(allocator.GetCount() == 0);
    }
}

TEST_CASE("HandleAllocator handle invalidation", "[data-structures][handle-allocator]") {
    HandleAllocator<DummyType, 8> allocator;

    SECTION("Removed handle becomes invalid") {
        auto handle = allocator.Add();
        REQUIRE(allocator.Remove(handle));

        REQUIRE_FALSE(allocator.IsValid(handle));
    }

    SECTION("Handle survives until removed") {
        auto h1 = allocator.Add();
        auto h2 = allocator.Add();

        allocator.Remove(h2);

        REQUIRE(allocator.IsValid(h1));
        REQUIRE_FALSE(allocator.IsValid(h2));
    }

    SECTION("Old handle invalid after slot reuse") {
        auto h1 = allocator.Add();
        uint32_t slot_index = h1.index;

        allocator.Remove(h1);
        auto h2 = allocator.Add();

        // If same slot reused, generations should differ
        if (h2.index == slot_index) {
            REQUIRE(h2.generation != h1.generation);
        }

        REQUIRE_FALSE(allocator.IsValid(h1));
        REQUIRE(allocator.IsValid(h2));
    }

    SECTION("Clear invalidates all handles") {
        auto h1 = allocator.Add();
        auto h2 = allocator.Add();

        allocator.Clear();

        REQUIRE_FALSE(allocator.IsValid(h1));
        REQUIRE_FALSE(allocator.IsValid(h2));
    }

    SECTION("Generation increments on reuse") {
        auto h1 = allocator.Add();
        uint32_t gen1 = h1.generation;
        uint32_t idx1 = h1.index;

        allocator.Remove(h1);

        // Keep allocating until we get same slot back
        std::vector<Handle<DummyType>> handles;
        for (int i = 0; i < 8; i++) {
            auto h = allocator.Add();
            handles.push_back(h);
            if (h.index == idx1) {
                REQUIRE(h.generation > gen1);
                break;
            }
        }

        // Clean up
        for (auto h : handles) {
            allocator.Remove(h);
        }
    }
}

TEST_CASE("HandleAllocator reuse order", "[data-structures][handle-allocator]") {
    HandleAllocator<DummyType, 8> allocator;

    SECTION("Even wear distribution over time") {
        std::map<uint32_t, int> index_usage;

        auto h = allocator.Add();
        for (int i = 0; i < 32; i++) {
            index_usage[h.index]++;
            allocator.Remove(h);
            h = allocator.Add();
        }
        allocator.Remove(h);

        // With FIFO, usage should be spread across multiple indices
        REQUIRE(index_usage.size() > 1);
    }
}

TEST_CASE("HandleAllocator count tracking", "[data-structures][handle-allocator]") {
    HandleAllocator<DummyType, 8> allocator;

    SECTION("Count increases with allocations") {
        REQUIRE(allocator.GetCount() == 0);
        allocator.Add();
        REQUIRE(allocator.GetCount() == 1);
        allocator.Add();
        REQUIRE(allocator.GetCount() == 2);
    }

    SECTION("Count decreases with removals") {
        auto h1 = allocator.Add();
        auto h2 = allocator.Add();
        REQUIRE(allocator.GetCount() == 2);

        allocator.Remove(h1);
        REQUIRE(allocator.GetCount() == 1);

        allocator.Remove(h2);
        REQUIRE(allocator.GetCount() == 0);
    }

    SECTION("Count unchanged on failed removal") {
        auto h = allocator.Add();
        allocator.Remove(h);

        REQUIRE(allocator.GetCount() == 0);
        allocator.Remove(h); // Already removed
        REQUIRE(allocator.GetCount() == 0);
    }

    SECTION("Count unchanged on failed allocation") {
        for (int i = 0; i < 8; i++) {
            allocator.Add();
        }
        REQUIRE(allocator.GetCount() == 8);

        allocator.Add(); // Should fail
        REQUIRE(allocator.GetCount() == 8);
    }
}