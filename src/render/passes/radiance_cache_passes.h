//
// Created by William on 2026-07-10.
//

#ifndef WILL_ENGINE_RADIANCE_CACHE_PASSES_H
#define WILL_ENGINE_RADIANCE_CACHE_PASSES_H

#include "render/render-graph/render_graph.h"
#include "render/shaders/radiance_cache_interop.h"

namespace Render
{
class PipelineManager;

inline const StringID RADIANCE_CACHE_ENTRIES = SID("radiance_cache_entries");
inline const StringID RADIANCE_CACHE_ENTRIES_HISTORY = SID("radiance_cache_entries_history");
inline const StringID RADIANCE_CACHE_KEYS = SID("radiance_cache_keys");
inline const StringID RADIANCE_CACHE_KEYS_HISTORY = SID("radiance_cache_keys_history");
inline const StringID RADIANCE_CACHE_CELLS = SID("radiance_cache_cells");
inline const StringID RADIANCE_CACHE_CELLS_HISTORY = SID("radiance_cache_cells_history");
inline const StringID RADIANCE_CACHE_ACTIVE = SID("radiance_cache_active");
inline const StringID RADIANCE_CACHE_ACTIVE_LIST = SID("radiance_cache_active_list");
inline const StringID RADIANCE_CACHE_ACTIVE_COUNT = SID("radiance_cache_active_count");
inline const StringID RADIANCE_CACHE_SHADE_ARGS = SID("radiance_cache_shade_args");
inline const StringID RADIANCE_CACHE_DESCRIPTORS = SID("radiance_cache_descriptors");
inline const StringID RADIANCE_CACHE_BUFFERS_CURRENT = SID("radiance_cache_buffers_current");
inline const StringID RADIANCE_CACHE_STATS = SID("radiance_cache_stats");
static_assert(Core::FRAME_BUFFER_COUNT == 3);
inline const StringID RADIANCE_CACHE_TOUCH_ENTRIES[3] = {SID("radiance_cache_touch_entries_0"), SID("radiance_cache_touch_entries_1"), SID("radiance_cache_touch_entries_2")};
inline const StringID RADIANCE_CACHE_TOUCH_KEYS[3] = {SID("radiance_cache_touch_keys_0"), SID("radiance_cache_touch_keys_1"), SID("radiance_cache_touch_keys_2")};

inline StringID RadianceCacheTouchEntries(uint64_t frameNumber) { return RADIANCE_CACHE_TOUCH_ENTRIES[frameNumber % Core::FRAME_BUFFER_COUNT]; }
inline StringID RadianceCacheTouchKeys(uint64_t frameNumber) { return RADIANCE_CACHE_TOUCH_KEYS[frameNumber % Core::FRAME_BUFFER_COUNT]; }

inline constexpr VkDeviceSize RADIANCE_CACHE_ENTRIES_BYTES = static_cast<VkDeviceSize>(RADIANCE_CACHE_HASH_CAPACITY) * sizeof(uint32_t);
inline constexpr VkDeviceSize RADIANCE_CACHE_KEYS_BYTES = static_cast<VkDeviceSize>(RADIANCE_CACHE_HASH_CAPACITY) * sizeof(uint2);
inline constexpr VkDeviceSize RADIANCE_CACHE_CELLS_BYTES = static_cast<VkDeviceSize>(RADIANCE_CACHE_HASH_CAPACITY) * sizeof(RadianceCacheCell);
inline constexpr VkDeviceSize RADIANCE_CACHE_DESCRIPTORS_BYTES = static_cast<VkDeviceSize>(RADIANCE_CACHE_HASH_CAPACITY) * sizeof(RadianceCacheHitDescriptor);
inline constexpr VkDeviceSize RADIANCE_CACHE_ACTIVE_LIST_BYTES = static_cast<VkDeviceSize>(RADIANCE_CACHE_SHADE_BUDGET) * sizeof(uint32_t);
inline constexpr VkDeviceSize RADIANCE_CACHE_SHADE_ARGS_BYTES = 3u * sizeof(uint32_t);

/** Gates every radiance-cache consumer this frame. */
struct RadianceCacheFrame
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
RadianceCacheFrame SetupRadianceCacheBegin(RenderGraph& graph, PipelineManager* pipelineManager, uint64_t frameNumber, const glm::vec3& cameraPos, bool bFreeze);

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
void SetupRadianceCacheShade(RenderGraph& graph, PipelineManager* pipelineManager, const RadianceCacheFrame& frame, uint32_t sceneIndex, bool bDDGIFeedbackValid, int32_t skyboxIndex, float iblIntensity, float maxRadiance, float bounceIntensity, uint32_t accumCap, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce);

/**
 * Carries this frame's cache buffers into history. Call last.
 * @param graph
 * @param frame
 */
void SetupRadianceCacheEnd(RenderGraph& graph, const RadianceCacheFrame& frame);

/**
 * One solid cube per occupied hash slot, colored by decoded radiance.
 * @param graph
 * @param pipelineManager
 * @param frame
 * @param debugExposure
 * @param normalBucket -1 draws every bucket; 0-5 draws only that normal bucket (+X,-X,+Y,-Y,+Z,-Z)
 */
void SetupRadianceCacheDebug(RenderGraph& graph, PipelineManager* pipelineManager, const RadianceCacheFrame& frame, float debugExposure, int32_t normalBucket);
} // Render

#endif //WILL_ENGINE_RADIANCE_CACHE_PASSES_H
