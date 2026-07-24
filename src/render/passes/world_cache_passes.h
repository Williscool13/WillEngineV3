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
inline const StringID WORLD_CACHE_ACTIVE_LIST = SID("world_cache_active_list");
inline const StringID WORLD_CACHE_ACTIVE_COUNT = SID("world_cache_active_count");
inline const StringID WORLD_CACHE_SHADE_ARGS = SID("world_cache_shade_args");
inline const StringID WORLD_CACHE_DESCRIPTORS = SID("world_cache_descriptors");
inline const StringID WORLD_CACHE_BUFFERS_CURRENT = SID("world_cache_buffers_current");
inline const StringID WORLD_CACHE_STATS = SID("world_cache_stats");

inline constexpr VkDeviceSize WORLD_CACHE_ENTRIES_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(uint32_t);
inline constexpr VkDeviceSize WORLD_CACHE_KEYS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(uint2);
inline constexpr VkDeviceSize WORLD_CACHE_CELLS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(WorldCacheCell);
inline constexpr VkDeviceSize WORLD_CACHE_DESCRIPTORS_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_HASH_CAPACITY) * sizeof(WorldCacheHitDescriptor);
inline constexpr VkDeviceSize WORLD_CACHE_ACTIVE_LIST_BYTES = static_cast<VkDeviceSize>(WORLD_CACHE_SHADE_BUDGET) * sizeof(uint32_t);
inline constexpr VkDeviceSize WORLD_CACHE_SHADE_ARGS_BYTES = 3u * sizeof(uint32_t);

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
 * @param cameraPos current camera world position; carry-forward drops cells whose distance-implied LOD no longer matches their stored level
 * @param bFreeze suspends carry-forward eviction (LRU + LOD) and pins cell ages, so a frozen GI field keeps its cache intact indefinitely
 * @return
 */
WorldCacheFrame SetupWorldCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber, const glm::vec3& cameraPos, bool bFreeze);

/**
 * Shades the frame's armed cells via budgeted indirect dispatch over the compact active list.
 * @param graph
 * @param pipelineManager
 * @param frame
 * @param sceneIndex
 * @param bDDGIFeedbackValid
 * @param skyboxIndex indirect-diffuse fallback for cells outside DDGI's coverage; -1 disables it
 * @param iblIntensity
 * @param maxRadiance firefly clamp applied before blending into the cell, so a single outlier sample can't drag the EMA toward it; 0 disables it
 * @param bounceIntensity sub-unity scale on the DDGI feedback term, bounding the cache<->DDGI loop gain
 * @param accumCap running-mean window cap for cell radiance (blend weight 1/(count+1) up to this); low values make cells track fresh shades fast at more variance
 * @param reflectionProbeCount baked probes this frame; inside a probe volume the cell's ambient term uses probe irradiance ahead of the skybox fallback
 * @param bReflectionProbeBruteForce debug: bypass the world-grid probe bin for the ambient's probe pick
 */
void SetupWorldCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const WorldCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity, float maxRadiance, float bounceIntensity, uint32_t accumCap, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce);

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
