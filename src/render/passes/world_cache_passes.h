//
// Created by William on 2026-07-10.
//

#ifndef WILL_ENGINE_WORLD_CACHE_PASSES_H
#define WILL_ENGINE_WORLD_CACHE_PASSES_H

#include "render/render-graph/render_graph.h"
#include "render/shaders/world_cache_interop.h"

namespace Render
{
class PipelineManager;

inline const StringID WORLD_CACHE_ENTRIES = SID("world_cache_entries");
inline const StringID WORLD_CACHE_ENTRIES_HISTORY = SID("world_cache_entries_history");
inline const StringID WORLD_CACHE_KEYS = SID("world_cache_keys");
inline const StringID WORLD_CACHE_KEYS_HISTORY = SID("world_cache_keys_history");
inline const StringID WORLD_CACHE_CELLS = SID("world_cache_cells");
inline const StringID WORLD_CACHE_CELLS_HISTORY = SID("world_cache_cells_history");
inline const StringID WORLD_CACHE_ACTIVE = SID("world_cache_active");
inline const StringID WORLD_CACHE_DESCRIPTORS = SID("world_cache_descriptors");
inline const StringID WORLD_CACHE_BUFFERS_CURRENT = SID("world_cache_buffers_current");

inline constexpr VkDeviceSize WORLD_CACHE_ENTRIES_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(uint32_t);
inline constexpr VkDeviceSize WORLD_CACHE_KEYS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(uint2);
inline constexpr VkDeviceSize WORLD_CACHE_CELLS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(WorldCacheCell);
inline constexpr VkDeviceSize WORLD_CACHE_DESCRIPTORS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(WorldCacheHitDescriptor);

/** Gates every world-cache consumer this frame. */
struct WorldCacheFrame
{
    bool bValid{false};
};


/**
 * Creates and clears this frame's cache buffers, carries forward last frame's survivors.
 * @param graph
 * @param pipelineManager
 * @param frameNumber
 * @return
 */
WorldCacheFrame SetupWorldCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber);

/**
 * Shades every radiance cache marked active.
 * @param graph
 * @param pipelineManager
 * @param frame
 * @param sceneIndex
 * @param bDDGIFeedbackValid
 * @param skyboxIndex indirect-diffuse fallback for cells outside DDGI's coverage; -1 disables it
 * @param iblIntensity
 */
void SetupWorldCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const WorldCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity);

/**
 * Carries this frame's cache buffers into history. Call last.
 * @param graph
 * @param frame
 */
void SetupWorldCacheEnd(RenderGraph& graph, const WorldCacheFrame& frame);

/**
 * One solid cube per occupied hash slot, colored by decoded radiance.
 * @param graph
 * @param pipelineManager
 * @param frame
 * @param debugExposure
 * @param normalBucket -1 draws every bucket; 0-5 draws only that normal bucket (+X,-X,+Y,-Y,+Z,-Z)
 */
void SetupWorldCacheDebug(RenderGraph& graph, PipelineManager* pipelineManager, const WorldCacheFrame& frame, float debugExposure, int32_t normalBucket);
} // Render

#endif //WILL_ENGINE_WORLD_CACHE_PASSES_H
