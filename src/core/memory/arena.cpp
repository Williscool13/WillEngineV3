//
// Created by William on 2026-03-29.
//

#include "arena.h"

namespace Core
{
Arena::Arena(size_t size) : head(0), capacity(size)
{
    assert(size > 0);
    memory = malloc(size);
    assert(memory != nullptr && "Arena: backing allocation failed");
}

Arena::~Arena()
{
    free(memory);
    memory = nullptr;
}

Arena::Arena(Arena&& other) noexcept
    : memory(other.memory), head(other.head), capacity(other.capacity)
{
    other.memory = nullptr;
    other.head = 0;
    other.capacity = 0;
}

Arena& Arena::operator=(Arena&& other) noexcept
{
    if (this != &other) {
        free(memory);
        memory = other.memory;
        head = other.head;
        capacity = other.capacity;
        other.memory = nullptr;
        other.head = 0;
        other.capacity = 0;
    }
    return *this;
}

void* Arena::AllocRaw(size_t size, size_t alignment)
{
    assert(size > 0);
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");

    const size_t alignedHead = (head + alignment - 1) & ~(alignment - 1);
    assert(alignedHead + size <= capacity && "Arena out of memory");

    if (alignedHead + size > capacity) {
        return nullptr;
    }

    head = alignedHead + size;
    return static_cast<char*>(memory) + alignedHead;
}

void Arena::Reset()
{
    head = 0;
}
} // Core
