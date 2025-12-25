//
// Created by William on 2025-12-24.
//

#include <catch2/catch_test_macros.hpp>

#include "core/allocators/handle.h"

using namespace Core;

struct DummyType {};

TEST_CASE("Handle validity", "[data-structures][handle]") {
    SECTION("INVALID constant is invalid") {
        REQUIRE_FALSE(Handle<DummyType>::INVALID.IsValid());
    }

    SECTION("Constructed invalid handle is invalid") {
        Handle<DummyType> h{INVALID_HANDLE_INDEX, INVALID_HANDLE_GENERATION};
        REQUIRE_FALSE(h.IsValid());
    }

    SECTION("Normal handle is valid") {
        Handle<DummyType> h{5, 3};
        REQUIRE(h.IsValid());
    }
}

TEST_CASE("Handle equality", "[data-structures][handle]") {
    Handle<DummyType> h1{10, 2};
    Handle<DummyType> h2{10, 2};
    Handle<DummyType> h3{10, 3};
    Handle<DummyType> h4{11, 2};

    REQUIRE(h1 == h2);
    REQUIRE_FALSE(h1 == h3);
    REQUIRE_FALSE(h1 == h4);
}

TEST_CASE("Handle ordering", "[data-structures][handle]") {
    REQUIRE(Handle<DummyType>{5, 10} < Handle<DummyType>{6, 1});
    REQUIRE(Handle<DummyType>{5, 2} < Handle<DummyType>{5, 3});
    REQUIRE_FALSE(Handle<DummyType>{5, 2} < Handle<DummyType>{5, 2});
}

TEST_CASE("Handle bit packing", "[data-structures][handle]") {
    SECTION("Max values fit correctly") {
        Handle<DummyType> h{0xFFFFFF, 0xFF};
        REQUIRE(h.index == 0xFFFFFF);
        REQUIRE(h.generation == 0xFF);
    }
}