//
// Created by William on 2026-08-21.
//

#ifndef WILL_ENGINE_VIRTUAL_MEMORY_H
#define WILL_ENGINE_VIRTUAL_MEMORY_H

#include <cstddef>

namespace Platform
{
/** bytes must be a multiple of VirtualReserveGranularity. Returns nullptr on failure. */
void* VirtualReserve(size_t bytes);

/** base and bytes must be page aligned. */
bool VirtualCommit(void* base, size_t bytes);

/** base and bytes must be page aligned. Contents are lost. */
bool VirtualDecommit(void* base, size_t bytes);

void VirtualRelease(void* base);

size_t VirtualPageSize();

size_t VirtualReserveGranularity();
} // Platform

#endif //WILL_ENGINE_VIRTUAL_MEMORY_H
