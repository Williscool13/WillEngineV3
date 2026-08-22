//
// Created by William on 2026-07-06.
//

#include "render/passes/ddgi_passes.h"

#include <tracy/Tracy.hpp>

#include <cfloat>

#include "render/render_utils.h"
#include "core/math/color_helpers.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
static StringID DDGI_IRRADIANCE[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_VISIBILITY[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_RAY_DATA[DDGI_MAX_VOLUME_SLOTS];
static const StringID DDGI_PROBE_OFFSETS_BUFFER = SID("ddgi_probe_offsets");
static const StringID DDGI_PROBE_RESTART_BUFFER = SID("ddgi_probe_restart");
static const StringID DDGI_PROBE_ACTIVE_BUFFER = SID("ddgi_probe_active");
static StringID DDGI_TRACE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_BLEND_IRRADIANCE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_BLEND_VISIBILITY_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_RELOCATE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_DEBUG_PASS[DDGI_MAX_VOLUME_SLOTS];
static constexpr uint32_t DDGI_ATLAS_BUCKETS = DDGI_MAX_RESIDENT_LOCAL_VOLUMES / DDGI_ATLAS_ROW_BUCKET;
static StringID DDGI_LOCAL_IRRADIANCE_BUCKET[DDGI_ATLAS_BUCKETS];
static StringID DDGI_LOCAL_VISIBILITY_BUCKET[DDGI_ATLAS_BUCKETS];

static StringID DDGILocalIrradianceId(uint32_t rows) { return DDGI_LOCAL_IRRADIANCE_BUCKET[rows / DDGI_ATLAS_ROW_BUCKET - 1u]; }

static StringID DDGILocalVisibilityId(uint32_t rows) { return DDGI_LOCAL_VISIBILITY_BUCKET[rows / DDGI_ATLAS_ROW_BUCKET - 1u]; }

static StringID DDGISlotName(const char* format, uint32_t slot)
{
    const Core::InlineString<48> name = Core::InlineString<48>::Format(format, slot);
    return StringID(name.c_str(), name.Size());
}

static bool InitDDGISlotNames()
{
    for (uint32_t k = 0; k < DDGI_MAX_VOLUME_SLOTS; ++k) {
        DDGI_IRRADIANCE[k] = DDGISlotName("ddgi_irradiance_%u", k);
        DDGI_VISIBILITY[k] = DDGISlotName("ddgi_visibility_%u", k);
        DDGI_RAY_DATA[k] = DDGISlotName("ddgi_ray_data_%u", k);
        DDGI_TRACE_PASS[k] = DDGISlotName("DDGI Probe Trace %u", k);
        DDGI_BLEND_IRRADIANCE_PASS[k] = DDGISlotName("DDGI Blend Irradiance %u", k);
        DDGI_BLEND_VISIBILITY_PASS[k] = DDGISlotName("DDGI Blend Visibility %u", k);
        DDGI_RELOCATE_PASS[k] = DDGISlotName("DDGI Probe Relocate %u", k);
        DDGI_DEBUG_PASS[k] = DDGISlotName("DDGI Probe Debug %u", k);
    }
    for (uint32_t b = 0; b < DDGI_ATLAS_BUCKETS; ++b) {
        const uint32_t rows = (b + 1u) * DDGI_ATLAS_ROW_BUCKET;
        DDGI_LOCAL_IRRADIANCE_BUCKET[b] = DDGISlotName("ddgi_local_irradiance_%u", rows);
        DDGI_LOCAL_VISIBILITY_BUCKET[b] = DDGISlotName("ddgi_local_visibility_%u", rows);
    }
    return true;
}

static const bool DDGI_SLOT_NAMES_INIT = InitDDGISlotNames();

DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const Core::LocalDDGIVolume* localVolumes, uint32_t localVolumeCount, const DDGICascades& previous, uint64_t frameNumber, bool bFreeze)
{
    const glm::ivec3 counts = glm::clamp(glm::ivec3(params.probeCountX, params.probeCountY, params.probeCountZ), glm::ivec3(2), glm::ivec3(32));
    const float baseSpacing = glm::max(params.probeSpacing, 0.1f);

    DDGICascades cascades{};
    cascades.count = glm::clamp(params.cascadeCount, 1u, DDGI_MAX_CAMERA_CASCADES);

    // Cold start (post full-clear or first startup): burst-update every cascade so cascade 0 validates immediately, else an odd-frame round-robin pick seeds indoor points from a coarse sky-lit outer cascade before the walls resolve.
    const bool bColdStart = previous.count == 0;
    const uint32_t updatedCascade = cascades.count == 1 || frameNumber % 2 == 0 ? 0u : 1u + static_cast<uint32_t>((frameNumber / 2) % (cascades.count - 1));
    for (uint32_t k = 0; k < cascades.count; ++k) {
        cascades.bUpdated[k] = params.bCascadeSampling && !bFreeze && (bColdStart || k == updatedCascade);

        const float cascadeScale = static_cast<float>(1u << k);
        const float biasScale = params.bScaleBiasPerCascade ? cascadeScale : 1.0f;
        const float spacing = baseSpacing * cascadeScale;
        DDGIVolumeParams volume{};
        volume.probeCount = glm::uvec3(counts);
        volume.probeSpacing = glm::vec3(spacing);
        volume.normalBias = glm::max(params.normalBias, 0.0f) * biasScale;
        volume.viewBias = glm::max(params.viewBias, 0.0f) * biasScale;
        volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
        volume.edgeFadeCells = glm::clamp(params.edgeBlendCells, 1.0f, 8.0f);
        volume.atlasSlot = 0u;
        volume.atlasRows = 1u;
        volume.baseCell = glm::ivec3(glm::floor(cameraPosition / spacing + 0.5f)) - counts / 2;

        if (!cascades.bUpdated[k] && k < previous.count && previous.volumes[k].probeCount == volume.probeCount && previous.volumes[k].probeSpacing == volume.probeSpacing) {
            volume.baseCell = previous.volumes[k].baseCell;
        }
        cascades.volumes[k] = volume;
    }

    cascades.localAtlasRows = DDGIAtlasRows(static_cast<uint32_t>(glm::max(params.maxResidentWorldVolumes, 1)));
    const uint32_t maxResident = glm::min(glm::min(static_cast<uint32_t>(glm::max(params.maxResidentWorldVolumes, 1)), cascades.localAtlasRows), DDGI_MAX_VOLUME_SLOTS - cascades.count);
    if (localVolumeCount > 0 && maxResident > 0) {
        const uint32_t candidates = glm::min(localVolumeCount, static_cast<uint32_t>(Core::MAX_LOCAL_DDGI_VOLUMES));
        bool taken[Core::MAX_LOCAL_DDGI_VOLUMES]{};
        uint32_t selected[DDGI_MAX_VOLUME_SLOTS]{};
        uint32_t selectedCount = 0;
        for (uint32_t s = 0; s < maxResident && s < candidates; ++s) {
            float bestDist = FLT_MAX;
            uint32_t best = UINT32_MAX;
            for (uint32_t i = 0; i < candidates; ++i) {
                if (taken[i]) {
                    continue;
                }
                const glm::vec3 halfExtents = glm::vec3(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * localVolumes[i].probeSpacing * 0.5f;
                const glm::vec3 center = glm::round(localVolumes[i].corner / localVolumes[i].probeSpacing) * localVolumes[i].probeSpacing + halfExtents;
                const glm::vec3 delta = glm::max(glm::abs(cameraPosition - center) - halfExtents, glm::vec3(0.0f));
                const float dist = glm::dot(delta, delta);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = i;
                }
            }
            if (best == UINT32_MAX) {
                break;
            }
            taken[best] = true;
            selected[selectedCount++] = best;
        }
        cascades.localCount = selectedCount;

        // Slot-sticky assignment: a still-resident volume keeps last frame's slot (and with it the atlas history); newcomers take the free slots and reconverge cold.
        const uint32_t localBase = cascades.count;
        bool slotUsed[DDGI_MAX_VOLUME_SLOTS]{};
        uint32_t slotOf[DDGI_MAX_VOLUME_SLOTS]{};
        for (uint32_t s = 0; s < selectedCount; ++s) {
            slotOf[s] = UINT32_MAX;
            for (uint32_t k = localBase; k < localBase + selectedCount; ++k) {
                if (!slotUsed[k] && previous.localIds[k] == localVolumes[selected[s]].volumeId) {
                    slotOf[s] = k;
                    slotUsed[k] = true;
                    break;
                }
            }
        }
        uint32_t nextFree = localBase;
        for (uint32_t s = 0; s < selectedCount; ++s) {
            if (slotOf[s] != UINT32_MAX) {
                continue;
            }
            while (slotUsed[nextFree]) {
                ++nextFree;
            }
            slotOf[s] = nextFree;
            slotUsed[nextFree] = true;
        }

        for (uint32_t s = 0; s < selectedCount; ++s) {
            const uint32_t k = slotOf[s];
            cascades.localWarmup[k] = previous.localIds[k] == localVolumes[selected[s]].volumeId ? previous.localWarmup[k] : 0u;
        }

        bool bWarmupPick[DDGI_MAX_VOLUME_SLOTS]{};
        uint32_t coldCount = 0;
        for (uint32_t s = 0; s < selectedCount; ++s) {
            if (cascades.localWarmup[slotOf[s]] < DDGI_LOCAL_WARMUP_UPDATES) {
                ++coldCount;
            }
        }
        const uint32_t boost = glm::max(static_cast<uint32_t>(glm::max(params.worldVolumeWarmupBoost, 1)), 1u);
        const uint32_t updateCount = glm::min(coldCount > 0 ? boost : 1u, selectedCount);
        for (uint32_t u = 0; u < updateCount; ++u) {
            uint32_t pick = UINT32_MAX;
            uint32_t bestWarmup = UINT32_MAX;
            for (uint32_t s = 0; s < selectedCount; ++s) {
                const uint32_t k = localBase + (static_cast<uint32_t>(frameNumber) + s) % selectedCount;
                if (!bWarmupPick[k] && cascades.localWarmup[k] < bestWarmup) {
                    bestWarmup = cascades.localWarmup[k];
                    pick = k;
                }
            }
            if (pick == UINT32_MAX) {
                break;
            }
            bWarmupPick[pick] = true;
            cascades.localWarmup[pick] = glm::min(cascades.localWarmup[pick] + 1u, DDGI_LOCAL_AGE_CAP);
        }
        for (uint32_t s = 0; s < selectedCount; ++s) {
            const Core::LocalDDGIVolume& local = localVolumes[selected[s]];
            const uint32_t k = slotOf[s];
            const float spacing = glm::max(local.probeSpacing, 0.25f);
            const glm::ivec3 baseCell = glm::ivec3(glm::round(local.corner / spacing));
            const glm::ivec3 localCounts = glm::ivec3(Core::LOCAL_DDGI_PROBES_PER_AXIS);

            DDGIVolumeParams volume{};
            volume.probeCount = glm::uvec3(localCounts);
            volume.probeSpacing = glm::vec3(spacing);
            volume.normalBias = glm::max(params.normalBias, 0.0f);
            volume.viewBias = glm::max(params.viewBias, 0.0f);
            volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
            volume.edgeFadeCells = 1.0f;
            volume.atlasSlot = k - localBase;
            volume.atlasRows = cascades.localAtlasRows;
            volume.baseCell = baseCell;

            cascades.volumes[k] = volume;
            cascades.localIds[k] = local.volumeId;
            cascades.bUpdated[k] = !bFreeze && (bColdStart || bWarmupPick[k]);
        }
    }
    return cascades;
}

/** Uniform random rotation (Shoemake) hashed from the frame number, so the ray set decorrelates across frames. */
static glm::vec4 DDGIRayRotation(uint64_t frameNumber)
{
    uint32_t state = static_cast<uint32_t>(frameNumber) * 747796405u + 2891336453u;
    float u[3];
    for (int i = 0; i < 3; ++i) {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        u[i] = static_cast<float>(word & 0x00FFFFFFu) / 16777216.0f;
    }
    const float s1 = std::sqrt(1.0f - u[0]);
    const float s2 = std::sqrt(u[0]);
    const float a = 6.28318530718f * u[1];
    const float b = 6.28318530718f * u[2];
    return {s1 * std::sin(a), s1 * std::cos(a), s2 * std::sin(b), s2 * std::cos(b)};
}

struct DDGICascadeDescSource
{
    DDGIVolumeParams volume{};
    StringID irradiance{};
    StringID visibility{};
    StringID offsets{};
    /** Byte offset of this slot's region in the flat offsets buffer. */
    uint32_t offsetsByteOffset{0};
    bool bValid{false};
};

/** Element offset of slot k in the flat probe-data buffers. Every cascade shares one probe count, so the cascade region is uniform; locals follow at the fixed 10^3 stride. */
static uint32_t DDGIProbeDataElemOffset(const DDGICascades& cascades, uint32_t k)
{
    const glm::uvec3 c = cascades.volumes[0].probeCount;
    const uint32_t cascadeStride = c.x * c.y * c.z;
    constexpr uint32_t localStride = Core::LOCAL_DDGI_PROBES_PER_AXIS * Core::LOCAL_DDGI_PROBES_PER_AXIS * Core::LOCAL_DDGI_PROBES_PER_AXIS;
    return k < cascades.count ? k * cascadeStride : cascades.count * cascadeStride + (k - cascades.count) * localStride;
}

struct DDGICascadeDescSources
{
    DDGICascadeDescSource entries[DDGI_MAX_VOLUME_SLOTS]{};
    uint32_t count{0};
    uint32_t localCount{0};
};

/**
 * Small vkCmdUpdateBuffer pass resolving the sources' descriptor indices/addresses at execute time into a DDGICascadeSetGPU. Sources must outlive execution (arena-allocated).
 */
static void AddDDGICascadeDescriptorUpload(RenderGraph& graph, StringID passName, StringID bufferId, const DDGICascadeDescSources* sources, const glm::vec3& gridCamPos, bool bGridCull)
{
    graph.CreateBuffer(bufferId, sizeof(DDGICascadeSetGPU), false);
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::DDGI);
    pass.WriteTransferBuffer(bufferId);
    const bool bVolumeGrid = bGridCull && graph.HasBuffer(WORLD_GRID_DDGI_GRID_BUFFER) && graph.HasBuffer(WORLD_GRID_DDGI_INDEX_BUFFER);
    pass.Execute([sources, bufferId, bVolumeGrid, gridCamPos](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DDGICascadeSetGPU set{};
        set.cascadeCount = sources->count;
        set.localCount = sources->localCount;
        if (bVolumeGrid) {
            set.volumeGrid = graph.PeekBufferAddress(WORLD_GRID_DDGI_GRID_BUFFER);
            set.volumeIndexList = graph.PeekBufferAddress(WORLD_GRID_DDGI_INDEX_BUFFER);
            set.gridCamPos = glm::vec4(gridCamPos, 0.0f);
            set.bVolumeGridValid = 1u;
        }
        for (uint32_t k = 0; k < sources->count + sources->localCount; ++k) {
            const DDGICascadeDescSource& source = sources->entries[k];
            DDGICascadeDescriptor& desc = set.cascades[k];
            desc.volume = source.volume;
            if (!source.bValid) {
                continue;
            }
            desc.irradianceIndex = graph.PeekSampledImageViewDescriptorIndex(source.irradiance);
            desc.visibilityIndex = graph.PeekSampledImageViewDescriptorIndex(source.visibility);
            desc.probeOffsets = source.offsets != StringID{} ? graph.PeekBufferAddress(source.offsets) + source.offsetsByteOffset : 0;
            desc.bOffsetsValid = source.offsets != StringID{} ? 1u : 0u;
            desc.bValid = 1u;
        }
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(bufferId), 0, sizeof(set), &set);
    });
}

void DeclareDDGIVolumeGridReads(RenderGraph& graph, RenderPass& pass)
{
    if (graph.HasBuffer(WORLD_GRID_DDGI_GRID_BUFFER)) { pass.ReadBuffer(WORLD_GRID_DDGI_GRID_BUFFER); }
    if (graph.HasBuffer(WORLD_GRID_DDGI_INDEX_BUFFER)) { pass.ReadBuffer(WORLD_GRID_DDGI_INDEX_BUFFER); }
}

void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const RadianceCacheFrame& radianceCache, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce, const glm::vec3& gridCamPos)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) {
        return;
    }
    if (cascades.count == 0) {
        return;
    }

    const uint32_t total = cascades.count + cascades.localCount;
    const uint32_t prevTotal = previous.count + previous.localCount;

    const StringID sharedIrradianceId = DDGILocalIrradianceId(cascades.localAtlasRows);
    const StringID sharedVisibilityId = DDGILocalVisibilityId(cascades.localAtlasRows);

    const bool bClassify = params.bClassification && params.bRelocation;

    const bool bLayoutStable = prevTotal > 0 && previous.count == cascades.count && previous.volumes[0].probeCount == cascades.volumes[0].probeCount;
    const bool bOffsetsCarried = graph.HasBuffer(DDGI_PROBE_OFFSETS_BUFFER) && bLayoutStable;
    const bool bRestartCarried = graph.HasBuffer(DDGI_PROBE_RESTART_BUFFER) && bLayoutStable;
    const bool bActiveCarried = graph.HasBuffer(DDGI_PROBE_ACTIVE_BUFFER) && bLayoutStable;

    bool bHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bOffsetsHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bRestartHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bActiveHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    for (uint32_t k = 0; k < total; ++k) {
        const bool bSameWindow = k < prevTotal && previous.localIds[k] == cascades.localIds[k] && previous.volumes[k].probeCount == cascades.volumes[k].probeCount
            && previous.volumes[k].probeSpacing == cascades.volumes[k].probeSpacing && previous.volumes[k].irradianceGamma == cascades.volumes[k].irradianceGamma;
        const bool bWritten = k < cascades.count || previous.localWarmup[k] > 0;
        bHistoryValid[k] = bSameWindow && bWritten && (k >= cascades.count
                                                          ? graph.HasTexture(sharedIrradianceId) && graph.HasTexture(sharedVisibilityId)
                                                          : graph.HasTexture(DDGI_IRRADIANCE[k]) && graph.HasTexture(DDGI_VISIBILITY[k]));
        bOffsetsHistoryValid[k] = bSameWindow && bWritten && params.bRelocation && bOffsetsCarried;
        bRestartHistoryValid[k] = bSameWindow && bWritten && params.bRelocation && bRestartCarried;
        bActiveHistoryValid[k] = bSameWindow && bWritten && bClassify && bActiveCarried;
    }

    if (params.bRelocation) {
        const uint32_t capacityElems = DDGIProbeDataElemOffset(cascades, cascades.count + DDGI_MAX_RESIDENT_LOCAL_VOLUMES);
        graph.CreateBuffer(DDGI_PROBE_OFFSETS_BUFFER, static_cast<VkDeviceSize>(capacityElems) * sizeof(glm::vec4), false);
        graph.CreateBuffer(DDGI_PROBE_RESTART_BUFFER, static_cast<VkDeviceSize>(capacityElems) * sizeof(uint32_t), false);
        graph.CarryBufferToNextFrame(DDGI_PROBE_OFFSETS_BUFFER, DDGI_PROBE_OFFSETS_BUFFER, 0);
        graph.CarryBufferToNextFrame(DDGI_PROBE_RESTART_BUFFER, DDGI_PROBE_RESTART_BUFFER, 0);
        if (bClassify) {
            graph.CreateBuffer(DDGI_PROBE_ACTIVE_BUFFER, static_cast<VkDeviceSize>(capacityElems) * sizeof(uint32_t), false);
            graph.CarryBufferToNextFrame(DDGI_PROBE_ACTIVE_BUFFER, DDGI_PROBE_ACTIVE_BUFFER, 0);
        }
    }

    const bool bFeedback = params.bInfiniteBounce && !bBounceOnly;
    const bool bWorldGrid = graph.HasBuffer(SID("world_grid_light_grid")) && graph.HasBuffer(SID("world_grid_index_list"));

    if (bFeedback) {
        DDGICascadeDescSources* prevSources = arena.AllocArray<DDGICascadeDescSources>(1);
        *prevSources = DDGICascadeDescSources{};
        prevSources->count = cascades.count;
        prevSources->localCount = cascades.localCount;
        for (uint32_t k = 0; k < total; ++k) {
            if (!params.bCascadeSampling && k < cascades.count) {
                prevSources->entries[k].volume = k < prevTotal ? previous.volumes[k] : cascades.volumes[k];
            } else if (bHistoryValid[k]) {
                prevSources->entries[k] = DDGICascadeDescSource{
                    .volume = previous.volumes[k],
                    .irradiance = k >= cascades.count ? sharedIrradianceId : DDGI_IRRADIANCE[k],
                    .visibility = k >= cascades.count ? sharedVisibilityId : DDGI_VISIBILITY[k],
                    .offsets = bOffsetsHistoryValid[k] ? DDGI_PROBE_OFFSETS_BUFFER : StringID{},
                    .offsetsByteOffset = DDGIProbeDataElemOffset(cascades, k) * static_cast<uint32_t>(sizeof(glm::vec4)),
                    .bValid = true,
                };
            } else {
                prevSources->entries[k].volume = k < prevTotal ? previous.volumes[k] : cascades.volumes[k];
            }
        }
        AddDDGICascadeDescriptorUpload(graph, SID("DDGI Prev Cascade Descriptors"), DDGI_CASCADES_PREV_BUFFER, prevSources, gridCamPos, params.bWorldVolumeGridCull);
    }

    constexpr VkImageUsageFlags atlasUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    for (uint32_t k = 0; k < cascades.count; ++k) {
        const glm::uvec3 probeCount = cascades.volumes[k].probeCount;
        graph.CreateTexture(DDGI_IRRADIANCE[k], TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, probeCount.x * probeCount.y * DDGI_IRRADIANCE_TILE, probeCount.z * DDGI_IRRADIANCE_TILE, 1}, {std::nullopt}, false);
        graph.CreateTexture(DDGI_VISIBILITY[k], TextureInfo{VK_FORMAT_R16G16_SFLOAT, probeCount.x * probeCount.y * DDGI_VISIBILITY_TILE, probeCount.z * DDGI_VISIBILITY_TILE, 1}, {std::nullopt}, false);
        graph.CarryTextureToNextFrame(DDGI_IRRADIANCE[k], DDGI_IRRADIANCE[k], atlasUsage);
        graph.CarryTextureToNextFrame(DDGI_VISIBILITY[k], DDGI_VISIBILITY[k], atlasUsage);
    }
    if (cascades.localCount > 0) {
        const uint32_t localProbes = static_cast<uint32_t>(Core::LOCAL_DDGI_PROBES_PER_AXIS);
        graph.CreateTexture(sharedIrradianceId, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, localProbes * localProbes * DDGI_IRRADIANCE_TILE, localProbes * DDGI_IRRADIANCE_TILE * cascades.localAtlasRows, 1}, {std::nullopt}, false);
        graph.CreateTexture(sharedVisibilityId, TextureInfo{VK_FORMAT_R16G16_SFLOAT, localProbes * localProbes * DDGI_VISIBILITY_TILE, localProbes * DDGI_VISIBILITY_TILE * cascades.localAtlasRows, 1}, {std::nullopt}, false);
        graph.CarryTextureToNextFrame(sharedIrradianceId, sharedIrradianceId, atlasUsage);
        graph.CarryTextureToNextFrame(sharedVisibilityId, sharedVisibilityId, atlasUsage);
    }

    for (uint32_t k = 0; k < total; ++k) {
        const DDGIVolumeParams& volume = cascades.volumes[k];
        const bool bLocal = k >= cascades.count;
        const uint32_t probeCountTotal = volume.probeCount.x * volume.probeCount.y * volume.probeCount.z;

        const StringID irradianceId = bLocal ? sharedIrradianceId : DDGI_IRRADIANCE[k];
        const StringID visibilityId = bLocal ? sharedVisibilityId : DDGI_VISIBILITY[k];

        if (!cascades.bUpdated[k]) {
            continue;
        }

        const uint32_t probeDataElem = DDGIProbeDataElemOffset(cascades, k);
        const uint32_t offsetsByteOffset = probeDataElem * static_cast<uint32_t>(sizeof(glm::vec4));
        const uint32_t flagByteOffset = probeDataElem * static_cast<uint32_t>(sizeof(uint32_t));

        const glm::vec4 rayRotation = DDGIRayRotation(frameNumber * DDGI_MAX_VOLUME_SLOTS + k);
        const glm::ivec3 previousBaseCell = k < prevTotal ? previous.volumes[k].baseCell : volume.baseCell;
        const uint32_t raysPerProbe = glm::clamp(k == 0 || bLocal ? params.raysPerProbe : params.outerRaysPerProbe, 16u, DDGI_MAX_RAYS_PER_PROBE);
        const float maxRayRadiance = glm::max(params.maxRayRadiance, 0.0f);
        const float bounceIntensity = glm::clamp(params.bounceIntensity, 0.0f, 1.0f);
        const uint32_t radianceCacheShadeInterval = glm::max(params.radianceCacheShadeInterval, 1u);

        graph.CreateBuffer(DDGI_RAY_DATA[k], static_cast<VkDeviceSize>(probeCountTotal) * raysPerProbe * sizeof(glm::vec4), false);

        RenderPass& tracePass = graph.AddPass(DDGI_TRACE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        tracePass.ReadTLASBuffer(RT_TLAS_BUFFER);
        tracePass.ReadBuffer(LIGHT_DATA_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
        tracePass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
        tracePass.WriteBuffer(DDGI_RAY_DATA[k]);
        tracePass.ReadBuffer(SCENE_DATA_BUFFER);
        tracePass.ReadBuffer(REFLECTION_PROBE_BUFFER);
        if (graph.HasBuffer(SID("world_grid_probe_grid"))) { tracePass.ReadBuffer(SID("world_grid_probe_grid")); }
        if (bWorldGrid) {
            tracePass.ReadBuffer(SID("world_grid_light_grid"));
            tracePass.ReadBuffer(SID("world_grid_index_list"));
        }
        if (radianceCache.bValid) {
            tracePass.ReadBuffer(RADIANCE_CACHE_BUFFERS_CURRENT);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_ENTRIES);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_KEYS);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_CELLS);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_ACTIVE);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_ACTIVE_LIST);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_ACTIVE_COUNT);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_DESCRIPTORS);
            tracePass.ReadWriteBuffer(RADIANCE_CACHE_STATS);
        }
        if (bFeedback) {
            tracePass.ReadBuffer(DDGI_CASCADES_PREV_BUFFER);
            DeclareDDGIVolumeGridReads(graph, tracePass);
            if (graph.HasTexture(sharedIrradianceId) && graph.HasTexture(sharedVisibilityId)) {
                tracePass.ReadSampledImage(sharedIrradianceId);
                tracePass.ReadSampledImage(sharedVisibilityId);
            }
            for (uint32_t j = 0; j < total; ++j) {
                if (bHistoryValid[j] && j < cascades.count) {
                    tracePass.ReadSampledImage(DDGI_IRRADIANCE[j]);
                    tracePass.ReadSampledImage(DDGI_VISIBILITY[j]);
                }
            }
        }
        if (graph.HasBuffer(DDGI_PROBE_OFFSETS_BUFFER)) { tracePass.ReadBuffer(DDGI_PROBE_OFFSETS_BUFFER); }
        if (graph.HasBuffer(DDGI_PROBE_ACTIVE_BUFFER)) { tracePass.ReadBuffer(DDGI_PROBE_ACTIVE_BUFFER); }
        tracePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, skyboxIndex, iblIntensity, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bWorldGrid, maxRayRadiance, bounceIntensity, radianceCacheShadeInterval, reflectionProbeCount, bReflectionProbeBruteForce, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], offsetsByteOffset, flagByteOffset, rayDataId = DDGI_RAY_DATA[k], frameNumber, bRadianceCache = radianceCache.bValid](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_trace"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeTracePushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCellXY = (static_cast<uint32_t>(previousBaseCell.x + 32768) & 0xFFFFu) | ((static_cast<uint32_t>(previousBaseCell.y + 32768) & 0xFFFFu) << 16u),
                .previousBaseCellZFlags = (static_cast<uint32_t>(previousBaseCell.z + 32768) & 0xFFFFu) | (bFeedback ? (1u << 16u) : 0u) | (bBounceOnly ? (1u << 17u) : 0u),
                .rayData = graph.GetBufferAddress(rayDataId),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
                .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
                .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
                .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
                .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
                .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
                .probeOffsets = bOffsetsHistory ? graph.GetBufferAddress(DDGI_PROBE_OFFSETS_BUFFER) + offsetsByteOffset : 0,
                .previousCascades = bFeedback ? graph.GetBufferAddress(DDGI_CASCADES_PREV_BUFFER) : 0,
                .radianceCache = bRadianceCache ? graph.GetBufferAddress(RADIANCE_CACHE_BUFFERS_CURRENT) : 0,
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .probeActive = bActiveHistory ? graph.GetBufferAddress(DDGI_PROBE_ACTIVE_BUFFER) + flagByteOffset : 0,
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
                .skyboxIndex = skyboxIndex,
                .raysAndShadeInterval = raysPerProbe | (radianceCacheShadeInterval << 16u),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .maxRayRadiance = maxRayRadiance,
                .bounceIntensity = bounceIntensity,
                .iblIntensity = iblIntensity,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                .worldGridProbeGrid = (!bReflectionProbeBruteForce && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_light_grid")) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(SID("world_grid_index_list")) : 0,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (raysPerProbe + 63) / 64, probeCountTotal, 1);
        });

        const uint32_t warmupUpdates = bLocal ? cascades.localWarmup[k] : 0u;
        const bool bWarming = warmupUpdates > 1u && warmupUpdates < DDGI_LOCAL_WARMUP_UPDATES;
        const float runningMeanHysteresis = static_cast<float>(warmupUpdates - 1u) / static_cast<float>(glm::max(warmupUpdates, 1u));
        const float blendHysteresis = bWarming ? glm::min(glm::clamp(params.hysteresis, 0.0f, 0.995f), runningMeanHysteresis) : glm::clamp(params.hysteresis, 0.0f, 0.995f);
        const float blendVisibilityHysteresis = bWarming ? glm::min(glm::clamp(params.visibilityHysteresis, 0.0f, 0.995f), runningMeanHysteresis) : glm::clamp(params.visibilityHysteresis, 0.0f, 0.995f);

        RenderPass& blendPass = graph.AddPass(DDGI_BLEND_IRRADIANCE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        blendPass.ReadBuffer(DDGI_RAY_DATA[k]);
        blendPass.WriteStorageImage(irradianceId);
        if (graph.HasBuffer(DDGI_PROBE_RESTART_BUFFER)) { blendPass.ReadBuffer(DDGI_PROBE_RESTART_BUFFER); }
        if (graph.HasBuffer(DDGI_PROBE_ACTIVE_BUFFER)) { blendPass.ReadBuffer(DDGI_PROBE_ACTIVE_BUFFER); }
        blendPass.Execute([pipelineManager, hysteresis = blendHysteresis, irradianceThreshold = params.irradianceThreshold, brightnessThreshold = params.brightnessThreshold, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, flagByteOffset, rayDataId = DDGI_RAY_DATA[k], atlasId = irradianceId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_irradiance"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayDataId),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(DDGI_PROBE_RESTART_BUFFER) + flagByteOffset : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .raysPerProbe = raysPerProbe,
                .hysteresis = hysteresis,
                .irradianceThreshold = irradianceThreshold,
                .brightnessThreshold = brightnessThreshold,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(DDGI_PROBE_ACTIVE_BUFFER) + flagByteOffset : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        RenderPass& visibilityPass = graph.AddPass(DDGI_BLEND_VISIBILITY_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        visibilityPass.ReadBuffer(DDGI_RAY_DATA[k]);
        visibilityPass.WriteStorageImage(visibilityId);
        if (graph.HasBuffer(DDGI_PROBE_RESTART_BUFFER)) { visibilityPass.ReadBuffer(DDGI_PROBE_RESTART_BUFFER); }
        if (graph.HasBuffer(DDGI_PROBE_ACTIVE_BUFFER)) { visibilityPass.ReadBuffer(DDGI_PROBE_ACTIVE_BUFFER); }
        visibilityPass.Execute([pipelineManager, visibilityHysteresis = blendVisibilityHysteresis, distanceExponent = glm::max(params.distanceExponent, 1.0f), volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, flagByteOffset, rayDataId = DDGI_RAY_DATA[k], atlasId = visibilityId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_blend_visibility"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayDataId),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(DDGI_PROBE_RESTART_BUFFER) + flagByteOffset : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .raysPerProbe = raysPerProbe,
                .hysteresis = visibilityHysteresis,
                .distanceExponent = distanceExponent,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(DDGI_PROBE_ACTIVE_BUFFER) + flagByteOffset : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        if (params.bRelocation) {
            // World-space standoff scales with the cascade like the biases, so coarse probes keep proportionate clearance; locals are finest and stay unscaled.
            const float minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f) * (bLocal ? 1.0f : static_cast<float>(1u << k));

            RenderPass& relocatePass = graph.AddPass(DDGI_RELOCATE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
            relocatePass.ReadBuffer(DDGI_RAY_DATA[k]);
            relocatePass.ReadWriteBuffer(DDGI_PROBE_OFFSETS_BUFFER);
            relocatePass.WriteBuffer(DDGI_PROBE_RESTART_BUFFER);
            if (bClassify) {
                relocatePass.ReadWriteBuffer(DDGI_PROBE_ACTIVE_BUFFER);
            }
            relocatePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], bClassify, raysPerProbe, probeCountTotal, minFrontfaceDistance, offsetsByteOffset, flagByteOffset, rayDataId = DDGI_RAY_DATA[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_relocate"));
                if (!pipelineEntry) {
                    return;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const VkDeviceAddress offsetsAddress = graph.GetBufferAddress(DDGI_PROBE_OFFSETS_BUFFER) + offsetsByteOffset;
                const VkDeviceAddress activeAddress = bClassify ? graph.GetBufferAddress(DDGI_PROBE_ACTIVE_BUFFER) + flagByteOffset : 0;
                DDGIProbeRelocatePushConstant pc{
                    .volume = volume,
                    .rayRotation = rayRotation,
                    .previousBaseCell = previousBaseCell,
                    .bOffsetsValid = bOffsetsHistory ? 1u : 0u,
                    .rayData = graph.GetBufferAddress(rayDataId),
                    .offsetsIn = bOffsetsHistory ? offsetsAddress : 0,
                    .offsetsOut = offsetsAddress,
                    .restartOut = graph.GetBufferAddress(DDGI_PROBE_RESTART_BUFFER) + flagByteOffset,
                    .raysPerProbe = raysPerProbe,
                    .minFrontfaceDistance = minFrontfaceDistance,
                    .activeIn = bActiveHistory ? activeAddress : 0,
                    .activeOut = bClassify ? activeAddress : 0,
                    .bActiveValid = bActiveHistory ? 1u : 0u,
                    .bClassify = bClassify ? 1u : 0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (probeCountTotal + 63) / 64, 1, 1);
            });
        }

    }

    // Current chain for lighting/remodulate: updated cascades read this frame's outputs, frozen cascades read their carried history.
    DDGICascadeDescSources* sources = arena.AllocArray<DDGICascadeDescSources>(1);
    *sources = DDGICascadeDescSources{};
    sources->count = cascades.count;
    sources->localCount = cascades.localCount;
    for (uint32_t k = 0; k < total; ++k) {
        if (!params.bCascadeSampling && k < cascades.count) {
            sources->entries[k].volume = cascades.volumes[k];
        } else if (k >= cascades.count) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = sharedIrradianceId,
                .visibility = sharedVisibilityId,
                .offsets = params.bRelocation && (cascades.bUpdated[k] || bOffsetsHistoryValid[k]) ? DDGI_PROBE_OFFSETS_BUFFER : StringID{},
                .offsetsByteOffset = DDGIProbeDataElemOffset(cascades, k) * static_cast<uint32_t>(sizeof(glm::vec4)),
                .bValid = cascades.bUpdated[k] || bHistoryValid[k],
            };
        } else if (cascades.bUpdated[k] || bHistoryValid[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = DDGI_IRRADIANCE[k],
                .visibility = DDGI_VISIBILITY[k],
                .offsets = params.bRelocation && (cascades.bUpdated[k] || bOffsetsHistoryValid[k]) ? DDGI_PROBE_OFFSETS_BUFFER : StringID{},
                .offsetsByteOffset = DDGIProbeDataElemOffset(cascades, k) * static_cast<uint32_t>(sizeof(glm::vec4)),
                .bValid = true,
            };
        } else {
            sources->entries[k].volume = cascades.volumes[k];
        }
    }
    AddDDGICascadeDescriptorUpload(graph, SID("DDGI Cascade Descriptors"), DDGI_CASCADES_BUFFER, sources, gridCamPos, params.bWorldVolumeGridCull);
}

bool AddDDGISampleDependencies(RenderGraph& graph, RenderPass& pass)
{
    if (!graph.HasBuffer(DDGI_CASCADES_BUFFER)) {
        return false;
    }
    pass.ReadBuffer(DDGI_CASCADES_BUFFER);
    DeclareDDGIVolumeGridReads(graph, pass);

    for (uint32_t b = 0; b < DDGI_ATLAS_BUCKETS; ++b) {
        const uint32_t rows = (b + 1u) * DDGI_ATLAS_ROW_BUCKET;
        if (graph.HasTexture(DDGILocalIrradianceId(rows)) && graph.HasTexture(DDGILocalVisibilityId(rows))) {
            pass.ReadSampledImage(DDGILocalIrradianceId(rows));
            pass.ReadSampledImage(DDGILocalVisibilityId(rows));
        }
    }

    for (uint32_t k = 0; k < DDGI_MAX_CAMERA_CASCADES; ++k) {
        if (graph.HasTexture(DDGI_IRRADIANCE[k]) && graph.HasTexture(DDGI_VISIBILITY[k])) {
            pass.ReadSampledImage(DDGI_IRRADIANCE[k]);
            pass.ReadSampledImage(DDGI_VISIBILITY[k]);
        }
    }
    if (graph.HasBuffer(DDGI_PROBE_OFFSETS_BUFFER)) {
        pass.ReadBuffer(DDGI_PROBE_OFFSETS_BUFFER);
    }
    return true;
}

/** Slot identification tint for the all-volumes debug view (unorm RGBA, low byte = red). Golden-ratio hue walk at low saturation so neighbouring slots stay distinguishable at any slot count; slot 0 stays white. */
static uint32_t DDGIPackTint(const glm::vec3& rgb)
{
    const glm::uvec3 quantized = glm::uvec3(glm::round(glm::clamp(rgb, 0.0f, 1.0f) * 255.0f));
    return 0xFF000000u | (quantized.z << 16u) | (quantized.y << 8u) | quantized.x;
}

static uint32_t DDGISlotTint(uint32_t slot)
{
    if (slot == 0) {
        return 0xFFFFFFFFu;
    }
    const float hue = glm::fract(static_cast<float>(slot) * 0.618033988f) * 6.0f;
    const float fraction = hue - glm::floor(hue);
    constexpr float saturation = 0.4f;
    const float high = 1.0f;
    const float low = 1.0f - saturation;
    const float rising = low + saturation * fraction;
    const float falling = high - saturation * fraction;
    glm::vec3 rgb;
    switch (static_cast<int32_t>(hue)) {
        case 0: rgb = {high, rising, low}; break;
        case 1: rgb = {falling, high, low}; break;
        case 2: rgb = {low, high, rising}; break;
        case 3: rgb = {low, falling, high}; break;
        case 4: rgb = {rising, low, high}; break;
        default: rgb = {high, low, falling}; break;
    }
    return DDGIPackTint(rgb);
}

/** World volumes tint by volumeId so probe spheres match the editor box and sprite colour regardless of slot. */
static uint32_t DDGIVolumeTint(uint64_t volumeId)
{
    return DDGIPackTint(glm::vec3(Core::Math::HashColor(volumeId, 0u, 0.08f, 0.84f)));
}

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive, int32_t probeDebugMode)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER)) {
        return;
    }

    for (uint32_t k = 0; k < cascades.count + cascades.localCount; ++k) {
        if (debugCascade >= 0 && k != static_cast<uint32_t>(debugCascade)) {
            continue;
        }
        if (debugCascade == DDGI_PROBE_DEBUG_LOCALS_ONLY && k < cascades.count) {
            continue;
        }
        const bool bLocal = k >= cascades.count;
        const StringID atlasId = bLocal ? DDGILocalIrradianceId(cascades.localAtlasRows) : DDGI_IRRADIANCE[k];
        if (!graph.HasTexture(atlasId)) {
            continue;
        }
        const StringID visibilityId = bLocal ? DDGILocalVisibilityId(cascades.localAtlasRows) : DDGI_VISIBILITY[k];
        const bool bVisibility = graph.HasTexture(visibilityId);
        // Flat buffers; a cold slot's region can be stale, acceptable for the debug draw.
        const bool bOffsets = graph.HasBuffer(DDGI_PROBE_OFFSETS_BUFFER);
        const bool bActive = graph.HasBuffer(DDGI_PROBE_ACTIVE_BUFFER);
        const uint32_t probeDataElem = DDGIProbeDataElemOffset(cascades, k);
        const DDGIVolumeParams& volume = cascades.volumes[k];

        RenderPass& pass = graph.AddPass(DDGI_DEBUG_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
        pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
        pass.ReadSampledImage(atlasId);
        if (bVisibility) {
            pass.ReadSampledImage(visibilityId);
        }
        if (bOffsets) {
            pass.ReadBuffer(DDGI_PROBE_OFFSETS_BUFFER);
        }
        if (bActive) {
            pass.ReadBuffer(DDGI_PROBE_ACTIVE_BUFFER);
        }
        const uint32_t packedTint = debugCascade < 0 ? (bLocal ? DDGIVolumeTint(cascades.localIds[k]) : DDGISlotTint(k)) : 0xFFFFFFFFu;
        const uint32_t warmupAge = bLocal ? cascades.localWarmup[k] : DDGI_LOCAL_AGE_CAP;
        pass.Execute([pipelineManager, volume, bOffsets, bActive, bHideInactive, probeDebugExposure, packedTint, warmupAge, atlasId, visibilityId, bVisibility, probeDebugMode, probeDataElem](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_debug"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeDebugPushConstant pc{
                .volume = volume,
                .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
                .sphereBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
                .probeOffsets = bOffsets ? graph.GetBufferAddress(DDGI_PROBE_OFFSETS_BUFFER) + probeDataElem * sizeof(glm::vec4) : 0,
                .irradianceAtlasIndex = graph.GetSampledImageViewDescriptorIndex(atlasId),
                .bOffsetsValid = bOffsets ? 1u : 0u,
                .probeDebugExposure = probeDebugExposure,
                .packedTint = packedTint,
                .probeActive = bActive ? graph.GetBufferAddress(DDGI_PROBE_ACTIVE_BUFFER) + probeDataElem * sizeof(uint32_t) : 0,
                .bActiveValid = bActive ? 1u : 0u,
                .bHideInactive = bHideInactive ? 1u : 0u,
                .visibilityAtlasIndex = bVisibility ? graph.GetSampledImageViewDescriptorIndex(visibilityId) : 0u,
                .debugMode = probeDebugMode == 3 ? 3u : (probeDebugMode == 2 ? 2u : (probeDebugMode == 1 && bVisibility ? 1u : 0u)),
                .warmupAge = warmupAge,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
        });
    }
#endif
}
} // Render
