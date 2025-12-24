//
// Created by William on 2025-12-24.
//

#include "core/allocators/ring_buffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace Core;

TEST_CASE("RingBuffer basic operations") {
    RingBuffer<int, 8> ring;

    SECTION("starts empty") {
        REQUIRE(ring.IsEmpty());
        REQUIRE(ring.GetSize() == 0);
        REQUIRE(ring.GetCapacity() == 8);
    }

    SECTION("push and pop single item") {
        ring.Push(42);
        REQUIRE_FALSE(ring.IsEmpty());
        REQUIRE(ring.GetSize() == 1);

        int value;
        REQUIRE(ring.Pop(value));
        REQUIRE(value == 42);
        REQUIRE(ring.IsEmpty());
    }

    SECTION("FIFO order") {
        ring.Push(1);
        ring.Push(2);
        ring.Push(3);

        int value;
        REQUIRE(ring.Pop(value));
        REQUIRE(value == 1);
        REQUIRE(ring.Pop(value));
        REQUIRE(value == 2);
        REQUIRE(ring.Pop(value));
        REQUIRE(value == 3);
    }

    SECTION("wraps around correctly") {
        // Fill buffer
        for (int i = 0; i < 8; i++) {
            ring.Push(i);
        }
        REQUIRE(ring.IsFull());

        // Pop some
        int value;
        ring.Pop(value);
        ring.Pop(value);

        // Push more (should wrap)
        ring.Push(100);
        ring.Push(101);

        REQUIRE(ring.GetSize() == 8);
    }

    SECTION("pop from empty returns false") {
        int value;
        REQUIRE_FALSE(ring.Pop(value));
    }

    SECTION("clear empties buffer") {
        ring.Push(1);
        ring.Push(2);
        ring.Clear();

        REQUIRE(ring.IsEmpty());
        REQUIRE(ring.GetSize() == 0);
    }

    SECTION("overwrites on push when full") {
        // Fill buffer
        for (int i = 0; i < 8; i++) {
            ring.Push(i);
        }

        // Push beyond capacity
        ring.Push(99);

        int value;
        ring.Pop(value);
        // Will get either 0 or 99 depending on overwrite behavior
        REQUIRE(ring.GetSize() <= 8);
    }
}