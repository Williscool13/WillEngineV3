//
// Created by William on 2025-12-16.
//

#include "stb_impl.h"

#include "core/memory/tlsf_allocator.h"

#include <cassert>

static Core::TlsfAllocator* gStbImageAllocator = nullptr;

static void* StbiAlloc(size_t bytes)
{
    assert(gStbImageAllocator && "SetStbImageAllocator not called");
    return gStbImageAllocator->Alloc(bytes, Core::AllocTag::Stbi);
}

static void* StbiRealloc(void* ptr, size_t bytes)
{
    return gStbImageAllocator->Realloc(ptr, bytes, Core::AllocTag::Stbi);
}

static void StbiFree(void* ptr)
{
    gStbImageAllocator->Free(ptr);
}

#define STBI_MALLOC(sz) StbiAlloc(sz)
#define STBI_REALLOC(p, newsz) StbiRealloc(p, newsz)
#define STBI_FREE(p) StbiFree(p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace Editor
{
void SetStbImageAllocator(Core::TlsfAllocator* allocator)
{
    gStbImageAllocator = allocator;
}
}
