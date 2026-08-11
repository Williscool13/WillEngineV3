//
// Created by William on 2026-06-22.
//

#ifndef WILL_ENGINE_TEXT3D_GEOMETRY_H
#define WILL_ENGINE_TEXT3D_GEOMETRY_H

#include "core/containers/vector.h"
#include "core/memory/tlsf_allocator.h"
#include "engine/resources/model/model_types.h"

namespace Engine
{
struct Font;
}

namespace AssetLoad
{
/**
 * Routes all earcut triangulation allocations through the given TLSF allocator.
 * Must be called before any BuildText3DGeometry call.
 * @param allocator
 */
void SetEarcutAllocator(Core::TlsfAllocator* allocator);

/**
 * Builds extruded 3D-text geometry on the calling (worker) thread, reading glyph contours directly from @p font (must stay resident for the call).
 * @return false if no geometry was produced (empty or whitespace-only text).
 */
bool BuildText3DGeometry(const Engine::Font& font, const Engine::Text3DParams& params, Core::TlsfAllocator& scratch,
                         Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);
} // AssetLoad

#endif //WILL_ENGINE_TEXT3D_GEOMETRY_H
