//
// Created by William on 2026-08-14.
//

#include "hb_impl.h"

#include "core/memory/tlsf_allocator.h"

#include <cassert>
#include <cstring>

static Core::TlsfAllocator* gHarfBuzzAllocator = nullptr;

extern "C" void* hb_malloc_impl(size_t size)
{
    assert(gHarfBuzzAllocator && "SetHarfBuzzAllocator not called");
    return gHarfBuzzAllocator->Alloc(size, Core::AllocTag::HarfBuzz);
}

extern "C" void* hb_calloc_impl(size_t nmemb, size_t size)
{
    const size_t bytes = nmemb * size;
    void* ptr = hb_malloc_impl(bytes);
    if (ptr) { memset(ptr, 0, bytes); }
    return ptr;
}

extern "C" void* hb_realloc_impl(void* ptr, size_t size)
{
    assert(gHarfBuzzAllocator && "SetHarfBuzzAllocator not called");
    return gHarfBuzzAllocator->Realloc(ptr, size, Core::AllocTag::HarfBuzz);
}

extern "C" void hb_free_impl(void* ptr)
{
    if (!ptr) { return; }
    gHarfBuzzAllocator->Free(ptr);
}

namespace Editor
{
void SetHarfBuzzAllocator(Core::TlsfAllocator* allocator)
{
    gHarfBuzzAllocator = allocator;
}
}
