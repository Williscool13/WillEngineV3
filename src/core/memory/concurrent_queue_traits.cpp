//
// Created by William on 2026-08-14.
//

#include "core/memory/concurrent_queue_traits.h"

#include <cassert>

namespace Core
{
static TlsfAllocator* gQueueAllocator = nullptr;

void SetConcurrentQueueAllocator(TlsfAllocator* alloc)
{
    gQueueAllocator = alloc;
}

void* TlsfQueueTraits::malloc(size_t size)
{
    assert(gQueueAllocator && "SetConcurrentQueueAllocator not called before queue construction");
    return gQueueAllocator->Alloc(size, AllocTag::Queue);
}

void TlsfQueueTraits::free(void* ptr)
{
    gQueueAllocator->Free(ptr);
}
} // Core
