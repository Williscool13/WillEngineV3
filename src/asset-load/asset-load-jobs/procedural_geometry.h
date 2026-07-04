//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_PROCEDURAL_GEOMETRY_H
#define WILL_ENGINE_PROCEDURAL_GEOMETRY_H

#include "core/containers/vector.h"
#include "core/memory/tlsf_allocator.h"
#include "engine/resources/model/model_types.h"

namespace AssetLoad
{
/**
 * Raw geometry generators for the non-analytic ("exotic") procedural types, factored out of ProceduralModelLoadSlot so both the render model slot and the physics collider slot can build the same mesh. Vulkan-free; write into caller-provided vectors backed by @p scratch. Return false on empty output.
 */
bool GenerateKleinBottleGeometry(const Engine::KleinBottleParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);
bool GenerateTrefoilKnotGeometry(const Engine::TrefoilKnotParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);
bool GenerateCurvedRampGeometry(const Engine::CurvedRampParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);
bool GenerateBowlGeometry(const Engine::BowlParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);

/** Dispatches to the matching exotic generator; returns false for analytic/monostate types (those have primitive/analytic colliders and never reach here). */
bool GenerateNonAnalyticProceduralGeometry(const Engine::ProceduralParams& params, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices);
} // AssetLoad

#endif //WILL_ENGINE_PROCEDURAL_GEOMETRY_H
