//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_HB_IMPL_H
#define WILL_ENGINE_HB_IMPL_H

namespace Core
{
class TlsfAllocator;
}

namespace Editor
{
/** Routes HarfBuzz internal allocations (hb_malloc_impl overrides) to the given allocator. Must be set before any hb call. */
void SetHarfBuzzAllocator(Core::TlsfAllocator* allocator);
}

#endif //WILL_ENGINE_HB_IMPL_H
