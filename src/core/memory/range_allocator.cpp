//
// Created by William on 2026-07-01.
//

#include "core/memory/range_allocator.h"

#include <cassert>

namespace Core
{
void RangeAllocator::Init(uint32_t capacity, TlsfAllocator* alloc, AllocTag tag, const char* name)
{
    assert(alloc != nullptr && "RangeAllocator: null allocator");
    capacity_ = capacity;
    used_ = 0;
    name_ = InlineString<32>(name);
    freeSpans_ = Vector<Range>(alloc, tag);
    if (capacity_ > 0) {
        freeSpans_.PushBack(Range{0, capacity_});
    }
}

void RangeAllocator::Reset()
{
    used_ = 0;
    freeSpans_.Clear();
    if (capacity_ > 0) {
        freeSpans_.PushBack(Range{0, capacity_});
    }
}

RangeAllocator::Range RangeAllocator::Allocate(uint32_t count)
{
    if (count == 0) {
        return {};
    }

    for (size_t i = 0; i < freeSpans_.Size(); ++i) {
        Range& span = freeSpans_[i];
        if (span.count < count) {
            continue;
        }

        const Range result{span.offset, count};
        if (span.count == count) {
            freeSpans_.RemoveAt(i);
        }
        else {
            span.offset += count;
            span.count -= count;
        }
        used_ += count;
        return result;
    }

    return {};
}

void RangeAllocator::Free(Range range)
{
    if (range.count == 0) {
        return;
    }
    assert(range.offset + range.count <= capacity_ && "RangeAllocator::Free out of bounds");

    size_t i = 0;
    while (i < freeSpans_.Size() && freeSpans_[i].offset < range.offset) {
        ++i;
    }

#ifndef NDEBUG
    if (i > 0) {
        const Range& left = freeSpans_[i - 1];
        assert(left.offset + left.count <= range.offset && "RangeAllocator::Free overlaps a free span (double free?)");
    }
    if (i < freeSpans_.Size()) {
        assert(range.offset + range.count <= freeSpans_[i].offset && "RangeAllocator::Free overlaps a free span (double free?)");
    }
#endif

    const bool touchesLeft = i > 0 && freeSpans_[i - 1].offset + freeSpans_[i - 1].count == range.offset;
    const bool touchesRight = i < freeSpans_.Size() && range.offset + range.count == freeSpans_[i].offset;

    if (touchesLeft && touchesRight) {
        freeSpans_[i - 1].count += range.count + freeSpans_[i].count;
        freeSpans_.RemoveAt(i);
    }
    else if (touchesLeft) {
        freeSpans_[i - 1].count += range.count;
    }
    else if (touchesRight) {
        freeSpans_[i].offset = range.offset;
        freeSpans_[i].count += range.count;
    }
    else {
        freeSpans_.Insert(freeSpans_.begin() + i, &range, &range + 1);
    }

    used_ -= range.count;
}

RangeAllocator::Stats RangeAllocator::GetStats() const
{
    uint32_t largest = 0;
    for (size_t i = 0; i < freeSpans_.Size(); ++i) {
        if (freeSpans_[i].count > largest) {
            largest = freeSpans_[i].count;
        }
    }
    return Stats{
        .capacity = capacity_,
        .used = used_,
        .largestFreeRun = largest,
        .freeSpanCount = static_cast<uint32_t>(freeSpans_.Size()),
    };
}
} // Core
