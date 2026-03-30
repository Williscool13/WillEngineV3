//
// Created by William on 2026-03-29.
//

#include "arena.h"

namespace Core
{
Arena::Arena(void* memory, size_t size)
    : memory(memory), head(0), capacity(size)
{
    assert(memory != nullptr);
    assert(size > 0);
}

void* Arena::AllocRaw(size_t size, size_t alignment)
{
    assert(size > 0);
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");

    const size_t alignedHead = (head + alignment - 1) & ~(alignment - 1);
    assert(alignedHead + size <= capacity && "Arena out of memory");

    if (alignedHead + size > capacity) { return nullptr; }

    head = alignedHead + size;
    return static_cast<char*>(memory) + alignedHead;
}

void Arena::Reset()
{
    head = 0;
}
} // Core
