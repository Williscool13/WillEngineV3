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
static const StringID DDGI_PROBE_OFFSETS_BUFFER = "ddgi_probe_offsets"_sid;
static const StringID DDGI_PROBE_RESTART_BUFFER = "ddgi_probe_restart"_sid;
static const StringID DDGI_PROBE_ACTIVE_BUFFER = "ddgi_probe_active"_sid;
static StringID DDGI_TRACE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_BLEND_IRRADIANCE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_BLEND_VISIBILITY_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_RELOCATE_PASS[DDGI_MAX_VOLUME_SLOTS];
static StringID DDGI_DEBUG_PASS[DDGI_MAX_VOLUME_SLOTS];

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
    return true;
}

static const bool DDGI_SLOT_NAMES_INIT = InitDDGISlotNames();

DDGICascades ComputeDDGICascades(const Core::DDGIParams& params, const glm::vec3& cameraPosition, const Core::LocalDDGIVolume* localVolumes, uint32_t localVolumeCount, const DDGICascades& previous, uint64_t frameNumber, bool bFreeze)
{
    const glm::ivec3 counts = glm::clamp(glm::ivec3(params.probeCountX, params.probeCountY, params.probeCountZ), glm::ivec3(2), glm::ivec3(32));
    const float baseSpacing = glm::max(params.probeSpacing, 0.1f);

    DDGICascades cascades{};
    cascades.count = glm::clamp(params.cascadeCount, 1u, DDGI_MAX_CAMERA_CASCADES);

    // Cold start updates every cascade, else round-robin seeds indoor points from a sky-lit outer cascade before the walls resolve.
    const bool bColdStart = previous.count == 0 || previous.count != cascades.count || previous.volumes[0].probeCount != glm::uvec3(counts) || previous.volumes[0].probeSpacing != baseSpacing;
    const uint32_t updatedCascade = cascades.count == 1 || frameNumber % 2 == 0 ? 0u : 1u + static_cast<uint32_t>((frameNumber / 2) % (cascades.count - 1));
    for (uint32_t k = 0; k < cascades.count; ++k) {
        const float cascadeScale = static_cast<float>(1u << k);
        const glm::ivec3 targetBaseCell = glm::ivec3(glm::floor(cameraPosition / (baseSpacing * cascadeScale) + 0.5f)) - counts / 2;
        // A jump of half the window or more replaces most probes at once (teleport, cut); treat it like a cold start for this cascade.
        const bool bJump = !bColdStart && glm::any(glm::greaterThanEqual(glm::abs(targetBaseCell - previous.volumes[k].baseCell), counts / 2));
        const bool bReset = bColdStart || bJump;
        cascades.bUpdated[k] = params.bCascadeSampling && !bFreeze && (bReset || k == updatedCascade);
        cascades.cascadeWarmup[k] = bReset ? 0u : previous.cascadeWarmup[k];
        cascades.lastUpdateFrame[k] = bReset ? frameNumber : previous.lastUpdateFrame[k];
        if (cascades.bUpdated[k]) {
            cascades.cascadeWarmup[k] = glm::min(cascades.cascadeWarmup[k] + 1u, DDGI_LOCAL_AGE_CAP);
            cascades.lastUpdateFrame[k] = frameNumber;
        }

        const float biasScale = params.bScaleBiasPerCascade ? cascadeScale : 1.0f;
        const float spacing = baseSpacing * cascadeScale;
        DDGIVolumeParams volume{};
        volume.probeCount = glm::uvec3(counts);
        volume.probeSpacing = spacing;
        volume.normalBias = glm::max(params.normalBias, 0.0f) * biasScale;
        volume.viewBias = glm::max(params.viewBias, 0.0f) * biasScale;
        volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
        volume.edgeFadeCells = glm::clamp(params.edgeBlendCells, 1.0f, 8.0f);
        volume.atlasSlot = 0u;
        volume.atlasRows = 1u;
        volume.baseCell = targetBaseCell;

        if (!cascades.bUpdated[k] && k < previous.count && previous.volumes[k].probeCount == volume.probeCount && previous.volumes[k].probeSpacing == volume.probeSpacing) {
            volume.baseCell = previous.volumes[k].baseCell;
        }
        cascades.volumes[k] = volume;
    }

    const uint32_t maxResident = glm::min(glm::min(static_cast<uint32_t>(glm::max(params.maxResidentWorldVolumes, 1)), DDGI_MAX_RESIDENT_LOCAL_VOLUMES), DDGI_MAX_VOLUME_SLOTS - cascades.count);
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
                const glm::vec3 center = localVolumes[i].corner + halfExtents;
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

        // Resident volumes keep last frame's slot so their atlas history stays valid.
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
            const bool bSameVolume = previous.localIds[k] == localVolumes[selected[s]].volumeId && previous.volumes[k].origin == localVolumes[selected[s]].corner;
            cascades.localWarmup[k] = bSameVolume ? previous.localWarmup[k] : 0u;
            cascades.lastUpdateFrame[k] = bSameVolume ? previous.lastUpdateFrame[k] : frameNumber;
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
            const glm::ivec3 localCounts = glm::ivec3(Core::LOCAL_DDGI_PROBES_PER_AXIS);

            DDGIVolumeParams volume{};
            volume.probeCount = glm::uvec3(localCounts);
            volume.probeSpacing = spacing;
            volume.origin = local.corner;
            volume.normalBias = glm::max(params.normalBias, 0.0f);
            volume.viewBias = glm::max(params.viewBias, 0.0f);
            volume.irradianceGamma = glm::max(params.irradianceGamma, 1.0f);
            volume.edgeFadeCells = 1.0f;
            volume.atlasSlot = 0u;
            volume.atlasRows = 1u;
            volume.baseCell = glm::ivec3(0);

            cascades.volumes[k] = volume;
            cascades.localIds[k] = local.volumeId;
            cascades.bUpdated[k] = !bFreeze && (bColdStart || bWarmupPick[k]);
            if (cascades.bUpdated[k]) {
                cascades.lastUpdateFrame[k] = frameNumber;
            }
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
    RDGTexture irradiance{};
    RDGTexture visibility{};
    RDGBuffer offsets{};
    /** Byte offset of this slot's region in the flat offsets buffer. */
    uint32_t offsetsByteOffset{0};
    bool bValid{false};
    uint32_t framesSinceUpdate{0};
};

/** Element offset of slot k in the flat probe-data buffers; locals follow the cascades at the fixed 10^3 stride. */
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

/** Resolves descriptor indices at execute time; sources must be filled before the call and outlive execution. */
static RDGBuffer AddDDGICascadeDescriptorUpload(RenderGraph& graph, StringID passName, StringID bufferId, const DDGICascadeDescSources* sources, RDGBuffer volumeGrid, RDGBuffer volumeIndexList, const glm::vec3& gridCamPos, bool bGridCull)
{
    const RDGBuffer buffer = graph.CreateBuffer(bufferId, sizeof(DDGICascadeSetGPU), false);
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::DDGI);
    pass.AsyncCompute();
    pass.WriteTransferBuffer(buffer);
    const bool bVolumeGrid = bGridCull && volumeGrid.IsValid() && volumeIndexList.IsValid();
    if (bVolumeGrid) {
        pass.ReferenceBuffer(buffer, volumeGrid);
        pass.ReferenceBuffer(buffer, volumeIndexList);
    }
    for (uint32_t k = 0; k < sources->count + sources->localCount; ++k) {
        const DDGICascadeDescSource& source = sources->entries[k];
        if (!source.bValid) {
            continue;
        }
        pass.ReferenceSampledImage(buffer, source.irradiance);
        pass.ReferenceSampledImage(buffer, source.visibility);
        if (source.offsets.IsValid()) { pass.ReferenceBuffer(buffer, source.offsets); }
    }
    pass.Execute([sources, buffer, bVolumeGrid, volumeGrid, volumeIndexList, gridCamPos](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DDGICascadeSetGPU set{};
        set.cascadeCount = sources->count;
        set.localCount = sources->localCount;
        if (bVolumeGrid) {
            set.volumeGrid = graph.GetBufferAddress(volumeGrid);
            set.volumeIndexList = graph.GetBufferAddress(volumeIndexList);
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
            desc.irradianceIndex = graph.GetSampledImageViewDescriptorIndex(source.irradiance);
            desc.visibilityIndex = graph.GetSampledImageViewDescriptorIndex(source.visibility);
            desc.probeOffsets = source.offsets.IsValid() ? graph.GetBufferAddress(source.offsets) + source.offsetsByteOffset : 0;
            desc.bOffsetsValid = source.offsets.IsValid() ? 1u : 0u;
            desc.bValid = 1u;
            desc.framesSinceUpdate = source.framesSinceUpdate;
        }
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(buffer), 0, sizeof(set), &set);
    });
    return buffer;
}

DDGIFrame SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const SceneResources& scene, const WorldGridFrame& worldGrid, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const RadianceCacheFrame& radianceCache, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce, const glm::vec3& gridCamPos, float framerateScale)
{
    ZoneScoped;
    if (!scene.tlas.IsValid() || !scene.instances.IsValid() || !scene.models.IsValid() || !scene.materials.IsValid()) {
        return {};
    }
    if (cascades.count == 0) {
        return {};
    }

    DDGIFrame frame{};
    frame.ddgiGrid = worldGrid.ddgiGrid;
    frame.ddgiIndexList = worldGrid.ddgiIndexList;

    const uint32_t total = cascades.count + cascades.localCount;
    const uint32_t prevTotal = previous.count + previous.localCount;

    const bool bClassify = params.bClassification && params.bRelocation;

    const bool bLayoutStable = prevTotal > 0 && previous.count == cascades.count && previous.volumes[0].probeCount == cascades.volumes[0].probeCount;

    constexpr VkImageUsageFlags atlasUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    RDGTextureRing irradianceRings[DDGI_MAX_VOLUME_SLOTS]{};
    RDGTextureRing visibilityRings[DDGI_MAX_VOLUME_SLOTS]{};
    for (uint32_t k = 0; k < total; ++k) {
        const glm::uvec3 probeCount = cascades.volumes[k].probeCount;
        const TextureInfo irradianceInfo{VK_FORMAT_R16G16B16A16_SFLOAT, probeCount.x * probeCount.y * DDGI_IRRADIANCE_TILE, probeCount.z * DDGI_IRRADIANCE_TILE, 1};
        const TextureInfo visibilityInfo{VK_FORMAT_R16G16_SFLOAT, probeCount.x * probeCount.y * DDGI_VISIBILITY_TILE, probeCount.z * DDGI_VISIBILITY_TILE, 1};
        irradianceRings[k] = graph.CreateVersionedTexture(DDGI_IRRADIANCE[k], irradianceInfo, 1, cascades.bUpdated[k] ? VersionSource::Fresh : VersionSource::NoShiftReadOnly, false, atlasUsage, true);
        visibilityRings[k] = graph.CreateVersionedTexture(DDGI_VISIBILITY[k], visibilityInfo, 1, cascades.bUpdated[k] ? VersionSource::Fresh : VersionSource::NoShiftReadOnly, false, atlasUsage, true);
    }

    RDGBufferRing offsetsRing{};
    RDGBufferRing restartRing{};
    RDGBufferRing activeRing{};
    bool bOffsetsCarried = false;
    bool bRestartCarried = false;
    bool bActiveCarried = false;
    if (params.bRelocation) {
        const uint32_t capacityElems = DDGIProbeDataElemOffset(cascades, cascades.count + DDGI_MAX_RESIDENT_LOCAL_VOLUMES);
        const VkDeviceSize offsetsBytes = static_cast<VkDeviceSize>(capacityElems) * sizeof(glm::vec4);
        const VkDeviceSize flagBytes = static_cast<VkDeviceSize>(capacityElems) * sizeof(uint32_t);
        offsetsRing = graph.CreateVersionedBuffer(DDGI_PROBE_OFFSETS_BUFFER, offsetsBytes, 1, VersionSource::Fresh, 0, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        restartRing = graph.CreateVersionedBuffer(DDGI_PROBE_RESTART_BUFFER, flagBytes, 1, VersionSource::Fresh, 0, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        if (bClassify) {
            activeRing = graph.CreateVersionedBuffer(DDGI_PROBE_ACTIVE_BUFFER, flagBytes, 1, VersionSource::Fresh, 0, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        }
        bOffsetsCarried = offsetsRing.Version(1).IsValid() && bLayoutStable;
        bRestartCarried = restartRing.Version(1).IsValid() && bLayoutStable;
        bActiveCarried = bClassify && activeRing.Version(1).IsValid() && bLayoutStable;

        if (bOffsetsCarried || bRestartCarried || bActiveCarried) {
            const RDGBuffer offsetsPrev = offsetsRing.Version(1);
            const RDGBuffer offsetsNext = offsetsRing.Current();
            const RDGBuffer restartPrev = restartRing.Version(1);
            const RDGBuffer restartNext = restartRing.Current();
            const RDGBuffer activePrev = activeRing.Version(1);
            const RDGBuffer activeNext = activeRing.Current();
            RenderPass& carry = graph.AddPass("DDGI Probe Data Carry"_sid, VK_PIPELINE_STAGE_2_COPY_BIT, RenderCategory::DDGI);
            carry.AsyncCompute();
            if (bOffsetsCarried) {
                carry.ReadTransferBuffer(offsetsPrev);
                carry.WriteTransferBuffer(offsetsNext);
            }
            if (bRestartCarried) {
                carry.ReadTransferBuffer(restartPrev);
                carry.WriteTransferBuffer(restartNext);
            }
            if (bActiveCarried) {
                carry.ReadTransferBuffer(activePrev);
                carry.WriteTransferBuffer(activeNext);
            }
            carry.Execute([bOffsetsCarried, bRestartCarried, bActiveCarried, offsetsBytes, flagBytes, offsetsPrev, offsetsNext, restartPrev, restartNext, activePrev, activeNext](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                auto copy = [&](RDGBuffer src, RDGBuffer dst, VkDeviceSize bytes) {
                    const VkBufferCopy region{.size = bytes};
                    vkCmdCopyBuffer(cmd, graph.GetBufferHandle(src), graph.GetBufferHandle(dst), 1, &region);
                };
                if (bOffsetsCarried) { copy(offsetsPrev, offsetsNext, offsetsBytes); }
                if (bRestartCarried) { copy(restartPrev, restartNext, flagBytes); }
                if (bActiveCarried) { copy(activePrev, activeNext, flagBytes); }
            });
        }
    }
    const RDGBuffer probeOffsets = offsetsRing.Current();
    const RDGBuffer probeRestart = restartRing.Current();
    const RDGBuffer probeActive = activeRing.Current();

    bool bHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bOffsetsHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bRestartHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bActiveHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    uint32_t prevAge[DDGI_MAX_VOLUME_SLOTS]{};
    for (uint32_t k = 0; k < total; ++k) {
        prevAge[k] = cascades.bUpdated[k] ? 1u : 0u;
        const bool bSameWindow = k < prevTotal && previous.localIds[k] == cascades.localIds[k] && previous.volumes[k].probeCount == cascades.volumes[k].probeCount
            && previous.volumes[k].probeSpacing == cascades.volumes[k].probeSpacing && previous.volumes[k].origin == cascades.volumes[k].origin && previous.volumes[k].irradianceGamma == cascades.volumes[k].irradianceGamma;
        const bool bWritten = k < cascades.count || previous.localWarmup[k] > 0;
        bHistoryValid[k] = bSameWindow && bWritten && irradianceRings[k].Version(prevAge[k]).IsValid() && visibilityRings[k].Version(prevAge[k]).IsValid();
        bOffsetsHistoryValid[k] = bSameWindow && bWritten && params.bRelocation && bOffsetsCarried;
        bRestartHistoryValid[k] = bSameWindow && bWritten && params.bRelocation && bRestartCarried;
        bActiveHistoryValid[k] = bSameWindow && bWritten && bClassify && bActiveCarried;
    }

    const bool bFeedback = params.bInfiniteBounce && !bBounceOnly;
    const bool bWorldGrid = worldGrid.lightGrid.IsValid() && worldGrid.indexList.IsValid();
    const RDGBuffer probeGrid = worldGrid.probeGrid;
    const RDGBuffer lightGrid = worldGrid.lightGrid;
    const RDGBuffer indexList = worldGrid.indexList;

    RDGBuffer cascadesPrev{};
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
                    .irradiance = irradianceRings[k].Version(prevAge[k]),
                    .visibility = visibilityRings[k].Version(prevAge[k]),
                    .offsets = bOffsetsHistoryValid[k] ? probeOffsets : RDGBuffer{},
                    .offsetsByteOffset = DDGIProbeDataElemOffset(cascades, k) * static_cast<uint32_t>(sizeof(glm::vec4)),
                    .bValid = true,
                };
            } else {
                prevSources->entries[k].volume = k < prevTotal ? previous.volumes[k] : cascades.volumes[k];
            }
        }
        cascadesPrev = AddDDGICascadeDescriptorUpload(graph, "DDGI Prev Cascade Descriptors"_sid, DDGI_CASCADES_PREV_BUFFER, prevSources, frame.ddgiGrid, frame.ddgiIndexList, gridCamPos, params.bWorldVolumeGridCull);
    }
    const RDGBuffer radianceCacheBuffers = radianceCache.IsValid() ? radianceCache.buffersCurrent : RDGBuffer{};

    for (uint32_t k = 0; k < total; ++k) {
        const DDGIVolumeParams& volume = cascades.volumes[k];
        const bool bLocal = k >= cascades.count;
        const uint32_t probeCountTotal = volume.probeCount.x * volume.probeCount.y * volume.probeCount.z;

        const RDGTexture irradianceNext = irradianceRings[k].Current();
        const RDGTexture irradianceHistory = irradianceRings[k].Version(1);
        const RDGTexture visibilityNext = visibilityRings[k].Current();
        const RDGTexture visibilityHistory = visibilityRings[k].Version(1);

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

        const RDGBuffer rayData = graph.CreateBuffer(DDGI_RAY_DATA[k], static_cast<VkDeviceSize>(probeCountTotal) * raysPerProbe * sizeof(glm::vec4), false);

        RenderPass& tracePass = graph.AddPass(DDGI_TRACE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        tracePass.AsyncCompute();
        tracePass.ReadTLASBuffer(scene.tlas);
        tracePass.ReadBuffer(scene.lightData);
        tracePass.ReadBuffer(scene.instances);
        tracePass.ReadBuffer(scene.primitives);
        tracePass.ReadBuffer(scene.models);
        tracePass.ReadBuffer(scene.materials);
        tracePass.ReadBuffer(scene.indices);
        tracePass.ReadBuffer(scene.vertexAttributes);
        tracePass.WriteBuffer(rayData);
        tracePass.ReadBuffer(scene.sceneData);
        tracePass.ReadBuffer(scene.reflectionProbes);
        if (probeGrid.IsValid()) { tracePass.ReadBuffer(probeGrid); }
        if (bWorldGrid) {
            tracePass.ReadBuffer(lightGrid);
            tracePass.ReadBuffer(indexList);
        }
        if (radianceCache.IsValid()) {
            tracePass.ReadBuffer(radianceCache.buffersCurrent);
            tracePass.ReadWriteBuffer(radianceCache.entries);
            tracePass.ReadWriteBuffer(radianceCache.keys);
            tracePass.ReadWriteBuffer(radianceCache.cells);
            tracePass.ReadWriteBuffer(radianceCache.active);
            tracePass.ReadWriteBuffer(radianceCache.activeList);
            tracePass.ReadWriteBuffer(radianceCache.activeCount);
            tracePass.ReadWriteBuffer(radianceCache.descriptors);
            tracePass.ReadWriteBuffer(radianceCache.stats);
        }
        if (bFeedback) {
            tracePass.ReadBuffer(cascadesPrev);
        }
        if (probeOffsets.IsValid()) { tracePass.ReadBuffer(probeOffsets); }
        if (probeActive.IsValid()) { tracePass.ReadBuffer(probeActive); }
        tracePass.Execute([pipelineManager, &scene, volume, rayRotation, previousBaseCell, skyboxIndex, iblIntensity, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bWorldGrid, maxRayRadiance, bounceIntensity, radianceCacheShadeInterval, reflectionProbeCount, bReflectionProbeBruteForce, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], offsetsByteOffset, flagByteOffset, rayData, frameNumber, radianceCacheBuffers, probeOffsets, probeActive, cascadesPrev, probeGrid, lightGrid, indexList](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ddgi_probe_trace"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeTracePushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCellXY = (static_cast<uint32_t>(previousBaseCell.x + 32768) & 0xFFFFu) | ((static_cast<uint32_t>(previousBaseCell.y + 32768) & 0xFFFFu) << 16u),
                .previousBaseCellZFlags = (static_cast<uint32_t>(previousBaseCell.z + 32768) & 0xFFFFu) | (bFeedback ? (1u << 16u) : 0u) | (bBounceOnly ? (1u << 17u) : 0u),
                .rayData = graph.GetBufferAddress(rayData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .instanceBuffer = graph.GetBufferAddress(scene.instances),
                .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
                .modelBuffer = graph.GetBufferAddress(scene.models),
                .materialBuffer = graph.GetBufferAddress(scene.materials),
                .indexBuffer = graph.GetBufferAddress(scene.indices),
                .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
                .probeOffsets = bOffsetsHistory ? graph.GetBufferAddress(probeOffsets) + offsetsByteOffset : 0,
                .previousCascades = bFeedback ? graph.GetBufferAddress(cascadesPrev) : 0,
                .radianceCache = radianceCacheBuffers.IsValid() ? graph.GetBufferAddress(radianceCacheBuffers) : 0,
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .probeActive = bActiveHistory ? graph.GetBufferAddress(probeActive) + flagByteOffset : 0,
                .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
                .skyboxIndex = skyboxIndex,
                .raysAndShadeInterval = raysPerProbe | (radianceCacheShadeInterval << 16u),
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .maxRayRadiance = maxRayRadiance,
                .bounceIntensity = bounceIntensity,
                .iblIntensity = iblIntensity,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                .worldGridProbeGrid = (!bReflectionProbeBruteForce && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
                .worldGridBuffer = bWorldGrid ? graph.GetBufferAddress(lightGrid) : 0,
                .worldGridIndexList = bWorldGrid ? graph.GetBufferAddress(indexList) : 0,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (raysPerProbe + 63) / 64, probeCountTotal, 1);
        });

        const uint32_t warmupUpdates = bLocal ? cascades.localWarmup[k] : cascades.cascadeWarmup[k];
        const bool bWarming = warmupUpdates > 1u && warmupUpdates < DDGI_LOCAL_WARMUP_UPDATES;
        const float runningMeanHysteresis = static_cast<float>(warmupUpdates - 1u) / static_cast<float>(glm::max(warmupUpdates, 1u));
        const float scaledHysteresis = glm::clamp(glm::pow(glm::clamp(params.hysteresis, 0.0f, 1.0f), 1.0f / framerateScale), 0.0f, 0.995f);
        const float scaledVisibilityHysteresis = glm::clamp(glm::pow(glm::clamp(params.visibilityHysteresis, 0.0f, 1.0f), 1.0f / framerateScale), 0.0f, 0.995f);
        const float blendHysteresis = bWarming ? glm::min(scaledHysteresis, runningMeanHysteresis) : scaledHysteresis;
        const float blendVisibilityHysteresis = bWarming ? glm::min(scaledVisibilityHysteresis, runningMeanHysteresis) : scaledVisibilityHysteresis;

        RenderPass& blendPass = graph.AddPass(DDGI_BLEND_IRRADIANCE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        blendPass.AsyncCompute();
        blendPass.ReadBuffer(rayData);
        if (bHistoryValid[k]) { blendPass.ReadStorageImage(irradianceHistory); }
        blendPass.WriteStorageImage(irradianceNext);
        if (probeRestart.IsValid()) { blendPass.ReadBuffer(probeRestart); }
        if (probeActive.IsValid()) { blendPass.ReadBuffer(probeActive); }
        blendPass.Execute([pipelineManager, hysteresis = blendHysteresis, irradianceThreshold = params.irradianceThreshold, brightnessThreshold = bWarming ? FLT_MAX : params.brightnessThreshold, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, flagByteOffset, rayData, probeRestart, probeActive, historyId = irradianceHistory, nextId = irradianceNext](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ddgi_blend_irradiance"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayData),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(probeRestart) + flagByteOffset : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(nextId),
                .atlasInIndex = bHistory ? graph.GetStorageImageViewDescriptorIndex(historyId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = hysteresis,
                .irradianceThreshold = irradianceThreshold,
                .brightnessThreshold = brightnessThreshold,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(probeActive) + flagByteOffset : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        RenderPass& visibilityPass = graph.AddPass(DDGI_BLEND_VISIBILITY_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        visibilityPass.AsyncCompute();
        visibilityPass.ReadBuffer(rayData);
        if (bHistoryValid[k]) { visibilityPass.ReadStorageImage(visibilityHistory); }
        visibilityPass.WriteStorageImage(visibilityNext);
        if (probeRestart.IsValid()) { visibilityPass.ReadBuffer(probeRestart); }
        if (probeActive.IsValid()) { visibilityPass.ReadBuffer(probeActive); }
        visibilityPass.Execute([pipelineManager, visibilityHysteresis = blendVisibilityHysteresis, distanceExponent = glm::max(params.distanceExponent, 1.0f), volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, flagByteOffset, rayData, probeRestart, probeActive, historyId = visibilityHistory, nextId = visibilityNext](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ddgi_blend_visibility"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeBlendPushConstant pc{
                .volume = volume,
                .rayRotation = rayRotation,
                .previousBaseCell = previousBaseCell,
                .bHistoryValid = bHistory ? 1u : 0u,
                .rayData = graph.GetBufferAddress(rayData),
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(probeRestart) + flagByteOffset : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(nextId),
                .atlasInIndex = bHistory ? graph.GetStorageImageViewDescriptorIndex(historyId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = visibilityHysteresis,
                .distanceExponent = distanceExponent,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(probeActive) + flagByteOffset : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        if (params.bRelocation) {
            // Standoff scales with the cascade like the biases; locals are finest and stay unscaled.
            const float minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f) * (bLocal ? 1.0f : static_cast<float>(1u << k));

            RenderPass& relocatePass = graph.AddPass(DDGI_RELOCATE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
            relocatePass.AsyncCompute();
            relocatePass.ReadBuffer(rayData);
            relocatePass.ReadWriteBuffer(probeOffsets);
            relocatePass.WriteBuffer(probeRestart);
            if (bClassify) {
                relocatePass.ReadWriteBuffer(probeActive);
            }
            relocatePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], bClassify, raysPerProbe, probeCountTotal, minFrontfaceDistance, offsetsByteOffset, flagByteOffset, rayData, probeOffsets, probeRestart, probeActive](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ddgi_probe_relocate"_sid);
                if (!pipelineEntry) {
                    return;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                const VkDeviceAddress offsetsAddress = graph.GetBufferAddress(probeOffsets) + offsetsByteOffset;
                const VkDeviceAddress activeAddress = bClassify ? graph.GetBufferAddress(probeActive) + flagByteOffset : 0;
                DDGIProbeRelocatePushConstant pc{
                    .volume = volume,
                    .rayRotation = rayRotation,
                    .previousBaseCell = previousBaseCell,
                    .bOffsetsValid = bOffsetsHistory ? 1u : 0u,
                    .rayData = graph.GetBufferAddress(rayData),
                    .offsetsIn = bOffsetsHistory ? offsetsAddress : 0,
                    .offsetsOut = offsetsAddress,
                    .restartOut = graph.GetBufferAddress(probeRestart) + flagByteOffset,
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
        } else if (cascades.bUpdated[k] || bHistoryValid[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = irradianceRings[k].Current(),
                .visibility = visibilityRings[k].Current(),
                .offsets = params.bRelocation && (cascades.bUpdated[k] || bOffsetsHistoryValid[k]) ? probeOffsets : RDGBuffer{},
                .offsetsByteOffset = DDGIProbeDataElemOffset(cascades, k) * static_cast<uint32_t>(sizeof(glm::vec4)),
                .bValid = true,
                .framesSinceUpdate = static_cast<uint32_t>(glm::min<uint64_t>(frameNumber - cascades.lastUpdateFrame[k], UINT32_MAX)),
            };
        } else {
            sources->entries[k].volume = cascades.volumes[k];
        }
    }
    frame.cascades = AddDDGICascadeDescriptorUpload(graph, "DDGI Cascade Descriptors"_sid, DDGI_CASCADES_BUFFER, sources, frame.ddgiGrid, frame.ddgiIndexList, gridCamPos, params.bWorldVolumeGridCull);
    frame.probeOffsets = probeOffsets;
    frame.probeActive = probeActive;
    for (uint32_t k = 0; k < total; ++k) {
        frame.irradiance[k] = irradianceRings[k].Current();
        frame.visibility[k] = visibilityRings[k].Current();
    }
    return frame;
}

/** Packs unorm RGBA, low byte = red. */
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

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGIFrame& ddgi, const GPUDebugFrame& gpuDebug, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive, int32_t probeDebugMode)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!gpuDebug.sphereArgs.IsValid()) {
        return;
    }

    const RDGBuffer sphereArgs = gpuDebug.sphereArgs;
    const RDGBuffer sphereInstances = gpuDebug.sphereInstances;
    const RDGBuffer probeOffsets = ddgi.probeOffsets;
    const RDGBuffer probeActive = ddgi.probeActive;
    for (uint32_t k = 0; k < cascades.count + cascades.localCount; ++k) {
        if (debugCascade >= 0 && k != static_cast<uint32_t>(debugCascade)) {
            continue;
        }
        if (debugCascade == DDGI_PROBE_DEBUG_LOCALS_ONLY && k < cascades.count) {
            continue;
        }
        const bool bLocal = k >= cascades.count;
        const RDGTexture atlasId = ddgi.irradiance[k];
        if (!atlasId.IsValid()) {
            continue;
        }
        const RDGTexture visibilityId = ddgi.visibility[k];
        const bool bVisibility = visibilityId.IsValid();
        // Flat buffers; a cold slot's region can be stale, acceptable for the debug draw.
        const bool bOffsets = probeOffsets.IsValid();
        const bool bActive = probeActive.IsValid();
        const uint32_t probeDataElem = DDGIProbeDataElemOffset(cascades, k);
        const DDGIVolumeParams& volume = cascades.volumes[k];

        RenderPass& pass = graph.AddPass(DDGI_DEBUG_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(sphereArgs);
        pass.WriteBuffer(sphereInstances);
        pass.ReadSampledImage(atlasId);
        if (bVisibility) {
            pass.ReadSampledImage(visibilityId);
        }
        if (bOffsets) {
            pass.ReadBuffer(probeOffsets);
        }
        if (bActive) {
            pass.ReadBuffer(probeActive);
        }
        const uint32_t packedTint = debugCascade < 0 ? (bLocal ? DDGIVolumeTint(cascades.localIds[k]) : DDGISlotTint(k)) : 0xFFFFFFFFu;
        const uint32_t warmupAge = bLocal ? cascades.localWarmup[k] : DDGI_LOCAL_AGE_CAP;
        pass.Execute([pipelineManager, volume, bOffsets, bActive, bHideInactive, probeDebugExposure, packedTint, warmupAge, atlasId, visibilityId, bVisibility, probeDebugMode, probeDataElem, sphereArgs, sphereInstances, probeOffsets,
                probeActive](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("ddgi_probe_debug"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeDebugPushConstant pc{
                .volume = volume,
                .sphereArgs = graph.GetBufferAddress(sphereArgs),
                .sphereBuffer = graph.GetBufferAddress(sphereInstances),
                .probeOffsets = bOffsets ? graph.GetBufferAddress(probeOffsets) + probeDataElem * sizeof(glm::vec4) : 0,
                .irradianceAtlasIndex = graph.GetSampledImageViewDescriptorIndex(atlasId),
                .bOffsetsValid = bOffsets ? 1u : 0u,
                .probeDebugExposure = probeDebugExposure,
                .packedTint = packedTint,
                .probeActive = bActive ? graph.GetBufferAddress(probeActive) + probeDataElem * sizeof(uint32_t) : 0,
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
