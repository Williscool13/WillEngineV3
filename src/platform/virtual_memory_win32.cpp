//
// Created by William on 2026-08-21.
//

#include "virtual_memory.h"

#include <Windows.h>

namespace Platform
{
static const SYSTEM_INFO& SysInfo()
{
    static const SYSTEM_INFO info = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return si;
    }();
    return info;
}

void* VirtualReserve(size_t bytes)
{
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
}

bool VirtualCommit(void* base, size_t bytes)
{
    return VirtualAlloc(base, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

bool VirtualDecommit(void* base, size_t bytes)
{
    return VirtualFree(base, bytes, MEM_DECOMMIT) != 0;
}

void VirtualRelease(void* base)
{
    VirtualFree(base, 0, MEM_RELEASE);
}

size_t VirtualPageSize()
{
    return SysInfo().dwPageSize;
}

size_t VirtualReserveGranularity()
{
    return SysInfo().dwAllocationGranularity;
}
} // Platform
