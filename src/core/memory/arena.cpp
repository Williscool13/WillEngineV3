//
// Created by William on 2026-03-29.
//

#include "arena.h"

#include <cassert>
#include <cstdio>

namespace Core
{
Arena::Arena(void* memory, size_t size, const char* name)
    : memory(memory), head(0), capacity(size), name(name)
{
    assert(memory != nullptr);
    assert(size > 0);
}

void* Arena::AllocRaw(size_t size, size_t alignment)
{
    assert(size > 0);
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");

    const size_t alignedHead = (head + alignment - 1) & ~(alignment - 1);
    if (alignedHead + size > capacity) {
        fprintf(stderr, "Arena '%s' OOM: Alloc %zu bytes (align %zu), used %zu / %zu\n", name.buf, size, alignment, head, capacity);
        assert(false && "Arena out of memory");
        return nullptr;
    }

    head = alignedHead + size;
    if (head > peakHead) { peakHead = head; }
    return static_cast<char*>(memory) + alignedHead;
}

void Arena::Reset()
{
    head = 0;
}
} // Core
