//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_CONCURRENT_QUEUE_TRAITS_H
#define WILL_ENGINE_CONCURRENT_QUEUE_TRAITS_H

#include <concurrentqueue/concurrentqueue.h>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * moodycamel concurrent queue allocated TlsfAllocator (AllocTag::Queue).
 */
struct TlsfQueueTraits : moodycamel::ConcurrentQueueDefaultTraits
{
    static constexpr size_t BLOCK_SIZE = 8;

    static void* malloc(size_t size);
    static void free(void* ptr);
};

void SetConcurrentQueueAllocator(TlsfAllocator* alloc);

template<typename T>
using ConcurrentQueue = moodycamel::ConcurrentQueue<T, TlsfQueueTraits>;
} // Core

#endif //WILL_ENGINE_CONCURRENT_QUEUE_TRAITS_H
