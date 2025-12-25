//
// Created by William on 2025-12-24.
//

#include <map>
#include <set>
#include <catch2/catch_test_macros.hpp>

#include "core/allocators/free_list.h"

using namespace Core;

struct TestData {
    int value = 0;
    TestData() = default;
    TestData(int v) : value(v) {}
};

TEST_CASE("FreeList allocation and deallocation", "[data-structures][free-list]") {
    FreeList<TestData, 8> list;

    SECTION("Starts with capacity available") {
        REQUIRE(list.IsAnyFree());
    }

    SECTION("Add returns valid handle") {
        auto handle = list.Add(TestData{42});
        REQUIRE(handle.IsValid());

        auto* item = list.Get(handle);
        REQUIRE(item != nullptr);
        REQUIRE(item->value == 42);
    }

    SECTION("Add without data returns valid handle") {
        auto handle = list.Add();
        REQUIRE(handle.IsValid());
        REQUIRE(list.Get(handle) != nullptr);
    }

    SECTION("Get with invalid handle returns null") {
        REQUIRE(list.Get(Handle<TestData>::INVALID) == nullptr);
    }

    SECTION("Fills to capacity") {
        std::vector<Handle<TestData>> handles;
        for (int i = 0; i < 8; i++) {
            auto h = list.Add(TestData{i});
            REQUIRE(h.IsValid());
            handles.push_back(h);
        }

        REQUIRE_FALSE(list.IsAnyFree());

        auto overflow = list.Add(TestData{999});
        REQUIRE_FALSE(overflow.IsValid());
    }

    SECTION("Add without data when full returns invalid") {
        // Fill to capacity
        for (int i = 0; i < 8; i++) {
            list.Add(TestData{i});
        }

        auto overflow = list.Add();
        REQUIRE_FALSE(overflow.IsValid());
    }

    SECTION("Remove frees slot for reuse") {
        auto h1 = list.Add(TestData{1});
        REQUIRE(list.Remove(h1));
        REQUIRE(list.IsAnyFree());

        auto h2 = list.Add(TestData{2});
        REQUIRE(h2.IsValid());
    }

    SECTION("Remove with invalid handle returns false") {
        REQUIRE_FALSE(list.Remove(Handle<TestData>::INVALID));
    }

    SECTION("Clear empties all slots") {
        list.Add(TestData{1});
        list.Add(TestData{2});

        list.Clear();
        REQUIRE(list.IsAnyFree());
    }
}

TEST_CASE("FreeList handle invalidation", "[data-structures][free-list]") {
    FreeList<TestData, 8> list;

    SECTION("Removed handle becomes invalid") {
        auto handle = list.Add(TestData{100});
        REQUIRE(list.Remove(handle));

        REQUIRE(list.Get(handle) == nullptr);
    }

    SECTION("Handle survives until removed") {
        auto h1 = list.Add(TestData{1});
        auto h2 = list.Add(TestData{2});

        list.Remove(h2);

        // h1 still valid
        auto* item = list.Get(h1);
        REQUIRE(item != nullptr);
        REQUIRE(item->value == 1);
    }

    SECTION("Old handle invalid after slot reuse") {
        auto h1 = list.Add(TestData{1});
        uint32_t slot_index = h1.index;

        list.Remove(h1);
        auto h2 = list.Add(TestData{2});

        // If same slot reused, generations should differ
        if (h2.index == slot_index) {
            REQUIRE(h2.generation != h1.generation);
        }

        // Old handle invalid
        REQUIRE(list.Get(h1) == nullptr);
        // New handle valid
        REQUIRE(list.Get(h2) != nullptr);
    }

    SECTION("Clear invalidates all handles") {
        auto h1 = list.Add(TestData{1});
        auto h2 = list.Add(TestData{2});

        list.Clear();

        REQUIRE(list.Get(h1) == nullptr);
        REQUIRE(list.Get(h2) == nullptr);
    }

    SECTION("Generation increments on reuse") {
        auto h1 = list.Add(TestData{1});
        uint32_t gen1 = h1.generation;
        uint32_t idx1 = h1.index;

        list.Remove(h1);

        // Keep allocating until we get same slot back
        std::vector<Handle<TestData>> handles;
        for (int i = 0; i < 8; i++) {
            auto h = list.Add(TestData{i});
            handles.push_back(h);
            if (h.index == idx1) {
                REQUIRE(h.generation > gen1);
                break;
            }
        }

        // Clean up
        for (auto h : handles) {
            list.Remove(h);
        }
    }
}

TEST_CASE("FreeList reuse order", "[data-structures][free-list]") {
    FreeList<TestData, 8> list;

    SECTION("Even wear distribution over time") {
        // Repeatedly allocate/deallocate
        std::map<uint32_t, int> index_usage;

        auto h = list.Add(TestData{0});
        for (int i = 0; i < 32; i++) {
            index_usage[h.index]++;
            list.Remove(h);
            h = list.Add(TestData{i});
        }
        list.Remove(h);

        // With FIFO, usage should be spread across multiple indices
        // With LIFO, one index would dominate
        REQUIRE(index_usage.size() > 1);
    }
}

TEST_CASE("FreeList data integrity", "[data-structures][free-list]") {
    FreeList<TestData, 8> list;

    SECTION("Data persists until removal") {
        auto h = list.Add(TestData{42});

        // Add more items
        list.Add(TestData{1});
        list.Add(TestData{2});

        // Original data unchanged
        REQUIRE(list.Get(h)->value == 42);
    }

    SECTION("Independent handles access independent data") {
        auto h1 = list.Add(TestData{100});
        auto h2 = list.Add(TestData{200});

        list.Get(h1)->value = 111;

        REQUIRE(list.Get(h1)->value == 111);
        REQUIRE(list.Get(h2)->value == 200);
    }
}