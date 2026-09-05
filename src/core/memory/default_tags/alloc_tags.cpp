//
// Created by William on 2026-09-05.
//

#include "core/memory/tlsf_allocator.h"

namespace Core
{
const char* AllocTagName(AllocTag tag)
{
    switch (tag) {
        case AllocTag::Unknown: return "Unknown";
        case AllocTag::Count: return "Count";
    }
    return "Unknown";
}
} // Core
