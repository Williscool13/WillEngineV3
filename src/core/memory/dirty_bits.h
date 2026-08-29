//
// Created by William on 2026-08-29.
//

#ifndef WILL_ENGINE_DIRTY_BITS_H
#define WILL_ENGINE_DIRTY_BITS_H

#include <bit>
#include <cstdint>

#include "core/containers/heap_array.h"

namespace Core
{
struct DirtyRun
{
    uint32_t offset{0};
    uint32_t count{0};
};

/**
 * Per-slot dirty tracking for a store whose payload fans out to several host buffer slots.
 * Mark() sets the bit in every set; Drain() consumes and clears one set, so a single mark uploads once per host slot without the caller tracking which slots are behind. Not thread-safe.
 */
class DirtyBits
{
public:
    void Init(uint32_t capacity, uint32_t setCount, TlsfAllocator* alloc, AllocTag tag)
    {
        capacity_ = capacity;
        setCount_ = setCount;
        wordsPerSet_ = (capacity + BITS_PER_WORD - 1) / BITS_PER_WORD;
        words_ = HeapArray<uint64_t>(alloc, tag, static_cast<size_t>(wordsPerSet_) * setCount);
    }

    void Mark(uint32_t slot)
    {
        if (slot >= capacity_) { return; }
        const uint32_t word = slot / BITS_PER_WORD;
        const uint64_t bit = 1ull << (slot % BITS_PER_WORD);
        for (uint32_t s = 0; s < setCount_; ++s) {
            words_[static_cast<size_t>(s) * wordsPerSet_ + word] |= bit;
        }
    }

    void MarkRange(uint32_t offset, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i) { Mark(offset + i); }
    }

    /** Emits maximal contiguous runs of set bits below limit and clears the set. emit(offset, count). */
    template<typename Fn>
    void Drain(uint32_t setIndex, uint32_t limit, Fn&& emit)
    {
        uint64_t* set = words_.Data() + static_cast<size_t>(setIndex % setCount_) * wordsPerSet_;
        const uint32_t neededWords = (limit + BITS_PER_WORD - 1) / BITS_PER_WORD;
        const uint32_t limitWords = neededWords < wordsPerSet_ ? neededWords : wordsPerSet_;
        uint32_t runStart = 0;
        uint32_t runCount = 0;

        for (uint32_t w = 0; w < limitWords; ++w) {
            const uint32_t wordBase = w * BITS_PER_WORD;
            // Bits at or above the limit are left set: the watermark can fall below a marked slot and rise again later.
            const uint32_t wordBits = limit - wordBase;
            const uint64_t mask = wordBits < BITS_PER_WORD ? (1ull << wordBits) - 1 : ~0ull;
            uint64_t bits = set[w] & mask;
            set[w] &= ~mask;
            while (bits != 0) {
                const uint32_t slot = wordBase + static_cast<uint32_t>(std::countr_zero(bits));
                bits &= bits - 1;
                if (runCount > 0 && slot == runStart + runCount) {
                    ++runCount;
                    continue;
                }
                if (runCount > 0) { emit(runStart, runCount); }
                runStart = slot;
                runCount = 1;
            }
        }

        if (runCount > 0) { emit(runStart, runCount); }
    }

private:
    static constexpr uint32_t BITS_PER_WORD = 64;

    HeapArray<uint64_t> words_{};
    uint32_t wordsPerSet_{0};
    uint32_t setCount_{0};
    uint32_t capacity_{0};
};
} // namespace Core

#endif //WILL_ENGINE_DIRTY_BITS_H
