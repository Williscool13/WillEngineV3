//
// Created by William on 2026-07-06.
//

#include "render/passes/ddgi_passes.h"

#include <cfloat>

#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
static const StringID DDGI_IRRADIANCE[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_irradiance_0"), SID("ddgi_irradiance_1"), SID("ddgi_irradiance_2"), SID("ddgi_irradiance_3"), SID("ddgi_irradiance_4"), SID("ddgi_irradiance_5"), SID("ddgi_irradiance_6"), SID("ddgi_irradiance_7"), SID("ddgi_irradiance_8"), SID("ddgi_irradiance_9"), SID("ddgi_irradiance_10"), SID("ddgi_irradiance_11"), SID("ddgi_irradiance_12"), SID("ddgi_irradiance_13"), SID("ddgi_irradiance_14"), SID("ddgi_irradiance_15"), SID("ddgi_irradiance_16"), SID("ddgi_irradiance_17"), SID("ddgi_irradiance_18"), SID("ddgi_irradiance_19"), SID("ddgi_irradiance_20"), SID("ddgi_irradiance_21"), SID("ddgi_irradiance_22"), SID("ddgi_irradiance_23"), SID("ddgi_irradiance_24"), SID("ddgi_irradiance_25"), SID("ddgi_irradiance_26"), SID("ddgi_irradiance_27"), SID("ddgi_irradiance_28"), SID("ddgi_irradiance_29"), SID("ddgi_irradiance_30"), SID("ddgi_irradiance_31")};
static const StringID DDGI_IRRADIANCE_HISTORY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_irradiance_history_0"), SID("ddgi_irradiance_history_1"), SID("ddgi_irradiance_history_2"), SID("ddgi_irradiance_history_3"), SID("ddgi_irradiance_history_4"), SID("ddgi_irradiance_history_5"), SID("ddgi_irradiance_history_6"), SID("ddgi_irradiance_history_7"), SID("ddgi_irradiance_history_8"), SID("ddgi_irradiance_history_9"), SID("ddgi_irradiance_history_10"), SID("ddgi_irradiance_history_11"), SID("ddgi_irradiance_history_12"), SID("ddgi_irradiance_history_13"), SID("ddgi_irradiance_history_14"), SID("ddgi_irradiance_history_15"), SID("ddgi_irradiance_history_16"), SID("ddgi_irradiance_history_17"), SID("ddgi_irradiance_history_18"), SID("ddgi_irradiance_history_19"), SID("ddgi_irradiance_history_20"), SID("ddgi_irradiance_history_21"), SID("ddgi_irradiance_history_22"), SID("ddgi_irradiance_history_23"), SID("ddgi_irradiance_history_24"), SID("ddgi_irradiance_history_25"), SID("ddgi_irradiance_history_26"), SID("ddgi_irradiance_history_27"), SID("ddgi_irradiance_history_28"), SID("ddgi_irradiance_history_29"), SID("ddgi_irradiance_history_30"), SID("ddgi_irradiance_history_31")};
static const StringID DDGI_VISIBILITY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_visibility_0"), SID("ddgi_visibility_1"), SID("ddgi_visibility_2"), SID("ddgi_visibility_3"), SID("ddgi_visibility_4"), SID("ddgi_visibility_5"), SID("ddgi_visibility_6"), SID("ddgi_visibility_7"), SID("ddgi_visibility_8"), SID("ddgi_visibility_9"), SID("ddgi_visibility_10"), SID("ddgi_visibility_11"), SID("ddgi_visibility_12"), SID("ddgi_visibility_13"), SID("ddgi_visibility_14"), SID("ddgi_visibility_15"), SID("ddgi_visibility_16"), SID("ddgi_visibility_17"), SID("ddgi_visibility_18"), SID("ddgi_visibility_19"), SID("ddgi_visibility_20"), SID("ddgi_visibility_21"), SID("ddgi_visibility_22"), SID("ddgi_visibility_23"), SID("ddgi_visibility_24"), SID("ddgi_visibility_25"), SID("ddgi_visibility_26"), SID("ddgi_visibility_27"), SID("ddgi_visibility_28"), SID("ddgi_visibility_29"), SID("ddgi_visibility_30"), SID("ddgi_visibility_31")};
static const StringID DDGI_VISIBILITY_HISTORY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_visibility_history_0"), SID("ddgi_visibility_history_1"), SID("ddgi_visibility_history_2"), SID("ddgi_visibility_history_3"), SID("ddgi_visibility_history_4"), SID("ddgi_visibility_history_5"), SID("ddgi_visibility_history_6"), SID("ddgi_visibility_history_7"), SID("ddgi_visibility_history_8"), SID("ddgi_visibility_history_9"), SID("ddgi_visibility_history_10"), SID("ddgi_visibility_history_11"), SID("ddgi_visibility_history_12"), SID("ddgi_visibility_history_13"), SID("ddgi_visibility_history_14"), SID("ddgi_visibility_history_15"), SID("ddgi_visibility_history_16"), SID("ddgi_visibility_history_17"), SID("ddgi_visibility_history_18"), SID("ddgi_visibility_history_19"), SID("ddgi_visibility_history_20"), SID("ddgi_visibility_history_21"), SID("ddgi_visibility_history_22"), SID("ddgi_visibility_history_23"), SID("ddgi_visibility_history_24"), SID("ddgi_visibility_history_25"), SID("ddgi_visibility_history_26"), SID("ddgi_visibility_history_27"), SID("ddgi_visibility_history_28"), SID("ddgi_visibility_history_29"), SID("ddgi_visibility_history_30"), SID("ddgi_visibility_history_31")};
static const StringID DDGI_OFFSETS[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_offsets_0"), SID("ddgi_probe_offsets_1"), SID("ddgi_probe_offsets_2"), SID("ddgi_probe_offsets_3"), SID("ddgi_probe_offsets_4"), SID("ddgi_probe_offsets_5"), SID("ddgi_probe_offsets_6"), SID("ddgi_probe_offsets_7"), SID("ddgi_probe_offsets_8"), SID("ddgi_probe_offsets_9"), SID("ddgi_probe_offsets_10"), SID("ddgi_probe_offsets_11"), SID("ddgi_probe_offsets_12"), SID("ddgi_probe_offsets_13"), SID("ddgi_probe_offsets_14"), SID("ddgi_probe_offsets_15"), SID("ddgi_probe_offsets_16"), SID("ddgi_probe_offsets_17"), SID("ddgi_probe_offsets_18"), SID("ddgi_probe_offsets_19"), SID("ddgi_probe_offsets_20"), SID("ddgi_probe_offsets_21"), SID("ddgi_probe_offsets_22"), SID("ddgi_probe_offsets_23"), SID("ddgi_probe_offsets_24"), SID("ddgi_probe_offsets_25"), SID("ddgi_probe_offsets_26"), SID("ddgi_probe_offsets_27"), SID("ddgi_probe_offsets_28"), SID("ddgi_probe_offsets_29"), SID("ddgi_probe_offsets_30"), SID("ddgi_probe_offsets_31")};
static const StringID DDGI_OFFSETS_HISTORY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_offsets_history_0"), SID("ddgi_probe_offsets_history_1"), SID("ddgi_probe_offsets_history_2"), SID("ddgi_probe_offsets_history_3"), SID("ddgi_probe_offsets_history_4"), SID("ddgi_probe_offsets_history_5"), SID("ddgi_probe_offsets_history_6"), SID("ddgi_probe_offsets_history_7"), SID("ddgi_probe_offsets_history_8"), SID("ddgi_probe_offsets_history_9"), SID("ddgi_probe_offsets_history_10"), SID("ddgi_probe_offsets_history_11"), SID("ddgi_probe_offsets_history_12"), SID("ddgi_probe_offsets_history_13"), SID("ddgi_probe_offsets_history_14"), SID("ddgi_probe_offsets_history_15"), SID("ddgi_probe_offsets_history_16"), SID("ddgi_probe_offsets_history_17"), SID("ddgi_probe_offsets_history_18"), SID("ddgi_probe_offsets_history_19"), SID("ddgi_probe_offsets_history_20"), SID("ddgi_probe_offsets_history_21"), SID("ddgi_probe_offsets_history_22"), SID("ddgi_probe_offsets_history_23"), SID("ddgi_probe_offsets_history_24"), SID("ddgi_probe_offsets_history_25"), SID("ddgi_probe_offsets_history_26"), SID("ddgi_probe_offsets_history_27"), SID("ddgi_probe_offsets_history_28"), SID("ddgi_probe_offsets_history_29"), SID("ddgi_probe_offsets_history_30"), SID("ddgi_probe_offsets_history_31")};
static const StringID DDGI_RESTART[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_restart_0"), SID("ddgi_probe_restart_1"), SID("ddgi_probe_restart_2"), SID("ddgi_probe_restart_3"), SID("ddgi_probe_restart_4"), SID("ddgi_probe_restart_5"), SID("ddgi_probe_restart_6"), SID("ddgi_probe_restart_7"), SID("ddgi_probe_restart_8"), SID("ddgi_probe_restart_9"), SID("ddgi_probe_restart_10"), SID("ddgi_probe_restart_11"), SID("ddgi_probe_restart_12"), SID("ddgi_probe_restart_13"), SID("ddgi_probe_restart_14"), SID("ddgi_probe_restart_15"), SID("ddgi_probe_restart_16"), SID("ddgi_probe_restart_17"), SID("ddgi_probe_restart_18"), SID("ddgi_probe_restart_19"), SID("ddgi_probe_restart_20"), SID("ddgi_probe_restart_21"), SID("ddgi_probe_restart_22"), SID("ddgi_probe_restart_23"), SID("ddgi_probe_restart_24"), SID("ddgi_probe_restart_25"), SID("ddgi_probe_restart_26"), SID("ddgi_probe_restart_27"), SID("ddgi_probe_restart_28"), SID("ddgi_probe_restart_29"), SID("ddgi_probe_restart_30"), SID("ddgi_probe_restart_31")};
static const StringID DDGI_RESTART_HISTORY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_restart_history_0"), SID("ddgi_probe_restart_history_1"), SID("ddgi_probe_restart_history_2"), SID("ddgi_probe_restart_history_3"), SID("ddgi_probe_restart_history_4"), SID("ddgi_probe_restart_history_5"), SID("ddgi_probe_restart_history_6"), SID("ddgi_probe_restart_history_7"), SID("ddgi_probe_restart_history_8"), SID("ddgi_probe_restart_history_9"), SID("ddgi_probe_restart_history_10"), SID("ddgi_probe_restart_history_11"), SID("ddgi_probe_restart_history_12"), SID("ddgi_probe_restart_history_13"), SID("ddgi_probe_restart_history_14"), SID("ddgi_probe_restart_history_15"), SID("ddgi_probe_restart_history_16"), SID("ddgi_probe_restart_history_17"), SID("ddgi_probe_restart_history_18"), SID("ddgi_probe_restart_history_19"), SID("ddgi_probe_restart_history_20"), SID("ddgi_probe_restart_history_21"), SID("ddgi_probe_restart_history_22"), SID("ddgi_probe_restart_history_23"), SID("ddgi_probe_restart_history_24"), SID("ddgi_probe_restart_history_25"), SID("ddgi_probe_restart_history_26"), SID("ddgi_probe_restart_history_27"), SID("ddgi_probe_restart_history_28"), SID("ddgi_probe_restart_history_29"), SID("ddgi_probe_restart_history_30"), SID("ddgi_probe_restart_history_31")};
static const StringID DDGI_ACTIVE[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_active_0"), SID("ddgi_probe_active_1"), SID("ddgi_probe_active_2"), SID("ddgi_probe_active_3"), SID("ddgi_probe_active_4"), SID("ddgi_probe_active_5"), SID("ddgi_probe_active_6"), SID("ddgi_probe_active_7"), SID("ddgi_probe_active_8"), SID("ddgi_probe_active_9"), SID("ddgi_probe_active_10"), SID("ddgi_probe_active_11"), SID("ddgi_probe_active_12"), SID("ddgi_probe_active_13"), SID("ddgi_probe_active_14"), SID("ddgi_probe_active_15"), SID("ddgi_probe_active_16"), SID("ddgi_probe_active_17"), SID("ddgi_probe_active_18"), SID("ddgi_probe_active_19"), SID("ddgi_probe_active_20"), SID("ddgi_probe_active_21"), SID("ddgi_probe_active_22"), SID("ddgi_probe_active_23"), SID("ddgi_probe_active_24"), SID("ddgi_probe_active_25"), SID("ddgi_probe_active_26"), SID("ddgi_probe_active_27"), SID("ddgi_probe_active_28"), SID("ddgi_probe_active_29"), SID("ddgi_probe_active_30"), SID("ddgi_probe_active_31")};
static const StringID DDGI_ACTIVE_HISTORY[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_probe_active_history_0"), SID("ddgi_probe_active_history_1"), SID("ddgi_probe_active_history_2"), SID("ddgi_probe_active_history_3"), SID("ddgi_probe_active_history_4"), SID("ddgi_probe_active_history_5"), SID("ddgi_probe_active_history_6"), SID("ddgi_probe_active_history_7"), SID("ddgi_probe_active_history_8"), SID("ddgi_probe_active_history_9"), SID("ddgi_probe_active_history_10"), SID("ddgi_probe_active_history_11"), SID("ddgi_probe_active_history_12"), SID("ddgi_probe_active_history_13"), SID("ddgi_probe_active_history_14"), SID("ddgi_probe_active_history_15"), SID("ddgi_probe_active_history_16"), SID("ddgi_probe_active_history_17"), SID("ddgi_probe_active_history_18"), SID("ddgi_probe_active_history_19"), SID("ddgi_probe_active_history_20"), SID("ddgi_probe_active_history_21"), SID("ddgi_probe_active_history_22"), SID("ddgi_probe_active_history_23"), SID("ddgi_probe_active_history_24"), SID("ddgi_probe_active_history_25"), SID("ddgi_probe_active_history_26"), SID("ddgi_probe_active_history_27"), SID("ddgi_probe_active_history_28"), SID("ddgi_probe_active_history_29"), SID("ddgi_probe_active_history_30"), SID("ddgi_probe_active_history_31")};
static const StringID DDGI_RAY_DATA[DDGI_MAX_VOLUME_SLOTS] = {SID("ddgi_ray_data_0"), SID("ddgi_ray_data_1"), SID("ddgi_ray_data_2"), SID("ddgi_ray_data_3"), SID("ddgi_ray_data_4"), SID("ddgi_ray_data_5"), SID("ddgi_ray_data_6"), SID("ddgi_ray_data_7"), SID("ddgi_ray_data_8"), SID("ddgi_ray_data_9"), SID("ddgi_ray_data_10"), SID("ddgi_ray_data_11"), SID("ddgi_ray_data_12"), SID("ddgi_ray_data_13"), SID("ddgi_ray_data_14"), SID("ddgi_ray_data_15"), SID("ddgi_ray_data_16"), SID("ddgi_ray_data_17"), SID("ddgi_ray_data_18"), SID("ddgi_ray_data_19"), SID("ddgi_ray_data_20"), SID("ddgi_ray_data_21"), SID("ddgi_ray_data_22"), SID("ddgi_ray_data_23"), SID("ddgi_ray_data_24"), SID("ddgi_ray_data_25"), SID("ddgi_ray_data_26"), SID("ddgi_ray_data_27"), SID("ddgi_ray_data_28"), SID("ddgi_ray_data_29"), SID("ddgi_ray_data_30"), SID("ddgi_ray_data_31")};
static const StringID DDGI_TRACE_PASS[DDGI_MAX_VOLUME_SLOTS] = {SID("DDGI Probe Trace 0"), SID("DDGI Probe Trace 1"), SID("DDGI Probe Trace 2"), SID("DDGI Probe Trace 3"), SID("DDGI Probe Trace 4"), SID("DDGI Probe Trace 5"), SID("DDGI Probe Trace 6"), SID("DDGI Probe Trace 7"), SID("DDGI Probe Trace 8"), SID("DDGI Probe Trace 9"), SID("DDGI Probe Trace 10"), SID("DDGI Probe Trace 11"), SID("DDGI Probe Trace 12"), SID("DDGI Probe Trace 13"), SID("DDGI Probe Trace 14"), SID("DDGI Probe Trace 15"), SID("DDGI Probe Trace 16"), SID("DDGI Probe Trace 17"), SID("DDGI Probe Trace 18"), SID("DDGI Probe Trace 19"), SID("DDGI Probe Trace 20"), SID("DDGI Probe Trace 21"), SID("DDGI Probe Trace 22"), SID("DDGI Probe Trace 23"), SID("DDGI Probe Trace 24"), SID("DDGI Probe Trace 25"), SID("DDGI Probe Trace 26"), SID("DDGI Probe Trace 27"), SID("DDGI Probe Trace 28"), SID("DDGI Probe Trace 29"), SID("DDGI Probe Trace 30"), SID("DDGI Probe Trace 31")};
static const StringID DDGI_BLEND_IRRADIANCE_PASS[DDGI_MAX_VOLUME_SLOTS] = {SID("DDGI Blend Irradiance 0"), SID("DDGI Blend Irradiance 1"), SID("DDGI Blend Irradiance 2"), SID("DDGI Blend Irradiance 3"), SID("DDGI Blend Irradiance 4"), SID("DDGI Blend Irradiance 5"), SID("DDGI Blend Irradiance 6"), SID("DDGI Blend Irradiance 7"), SID("DDGI Blend Irradiance 8"), SID("DDGI Blend Irradiance 9"), SID("DDGI Blend Irradiance 10"), SID("DDGI Blend Irradiance 11"), SID("DDGI Blend Irradiance 12"), SID("DDGI Blend Irradiance 13"), SID("DDGI Blend Irradiance 14"), SID("DDGI Blend Irradiance 15"), SID("DDGI Blend Irradiance 16"), SID("DDGI Blend Irradiance 17"), SID("DDGI Blend Irradiance 18"), SID("DDGI Blend Irradiance 19"), SID("DDGI Blend Irradiance 20"), SID("DDGI Blend Irradiance 21"), SID("DDGI Blend Irradiance 22"), SID("DDGI Blend Irradiance 23"), SID("DDGI Blend Irradiance 24"), SID("DDGI Blend Irradiance 25"), SID("DDGI Blend Irradiance 26"), SID("DDGI Blend Irradiance 27"), SID("DDGI Blend Irradiance 28"), SID("DDGI Blend Irradiance 29"), SID("DDGI Blend Irradiance 30"), SID("DDGI Blend Irradiance 31")};
static const StringID DDGI_BLEND_VISIBILITY_PASS[DDGI_MAX_VOLUME_SLOTS] = {SID("DDGI Blend Visibility 0"), SID("DDGI Blend Visibility 1"), SID("DDGI Blend Visibility 2"), SID("DDGI Blend Visibility 3"), SID("DDGI Blend Visibility 4"), SID("DDGI Blend Visibility 5"), SID("DDGI Blend Visibility 6"), SID("DDGI Blend Visibility 7"), SID("DDGI Blend Visibility 8"), SID("DDGI Blend Visibility 9"), SID("DDGI Blend Visibility 10"), SID("DDGI Blend Visibility 11"), SID("DDGI Blend Visibility 12"), SID("DDGI Blend Visibility 13"), SID("DDGI Blend Visibility 14"), SID("DDGI Blend Visibility 15"), SID("DDGI Blend Visibility 16"), SID("DDGI Blend Visibility 17"), SID("DDGI Blend Visibility 18"), SID("DDGI Blend Visibility 19"), SID("DDGI Blend Visibility 20"), SID("DDGI Blend Visibility 21"), SID("DDGI Blend Visibility 22"), SID("DDGI Blend Visibility 23"), SID("DDGI Blend Visibility 24"), SID("DDGI Blend Visibility 25"), SID("DDGI Blend Visibility 26"), SID("DDGI Blend Visibility 27"), SID("DDGI Blend Visibility 28"), SID("DDGI Blend Visibility 29"), SID("DDGI Blend Visibility 30"), SID("DDGI Blend Visibility 31")};
static const StringID DDGI_RELOCATE_PASS[DDGI_MAX_VOLUME_SLOTS] = {SID("DDGI Probe Relocate 0"), SID("DDGI Probe Relocate 1"), SID("DDGI Probe Relocate 2"), SID("DDGI Probe Relocate 3"), SID("DDGI Probe Relocate 4"), SID("DDGI Probe Relocate 5"), SID("DDGI Probe Relocate 6"), SID("DDGI Probe Relocate 7"), SID("DDGI Probe Relocate 8"), SID("DDGI Probe Relocate 9"), SID("DDGI Probe Relocate 10"), SID("DDGI Probe Relocate 11"), SID("DDGI Probe Relocate 12"), SID("DDGI Probe Relocate 13"), SID("DDGI Probe Relocate 14"), SID("DDGI Probe Relocate 15"), SID("DDGI Probe Relocate 16"), SID("DDGI Probe Relocate 17"), SID("DDGI Probe Relocate 18"), SID("DDGI Probe Relocate 19"), SID("DDGI Probe Relocate 20"), SID("DDGI Probe Relocate 21"), SID("DDGI Probe Relocate 22"), SID("DDGI Probe Relocate 23"), SID("DDGI Probe Relocate 24"), SID("DDGI Probe Relocate 25"), SID("DDGI Probe Relocate 26"), SID("DDGI Probe Relocate 27"), SID("DDGI Probe Relocate 28"), SID("DDGI Probe Relocate 29"), SID("DDGI Probe Relocate 30"), SID("DDGI Probe Relocate 31")};
static const StringID DDGI_LOCAL_IRRADIANCE = SID("ddgi_local_irradiance");
static const StringID DDGI_LOCAL_VISIBILITY = SID("ddgi_local_visibility");
static const StringID DDGI_DEBUG_PASS[DDGI_MAX_VOLUME_SLOTS] = {SID("DDGI Probe Debug 0"), SID("DDGI Probe Debug 1"), SID("DDGI Probe Debug 2"), SID("DDGI Probe Debug 3"), SID("DDGI Probe Debug 4"), SID("DDGI Probe Debug 5"), SID("DDGI Probe Debug 6"), SID("DDGI Probe Debug 7"), SID("DDGI Probe Debug 8"), SID("DDGI Probe Debug 9"), SID("DDGI Probe Debug 10"), SID("DDGI Probe Debug 11"), SID("DDGI Probe Debug 12"), SID("DDGI Probe Debug 13"), SID("DDGI Probe Debug 14"), SID("DDGI Probe Debug 15"), SID("DDGI Probe Debug 16"), SID("DDGI Probe Debug 17"), SID("DDGI Probe Debug 18"), SID("DDGI Probe Debug 19"), SID("DDGI Probe Debug 20"), SID("DDGI Probe Debug 21"), SID("DDGI Probe Debug 22"), SID("DDGI Probe Debug 23"), SID("DDGI Probe Debug 24"), SID("DDGI Probe Debug 25"), SID("DDGI Probe Debug 26"), SID("DDGI Probe Debug 27"), SID("DDGI Probe Debug 28"), SID("DDGI Probe Debug 29"), SID("DDGI Probe Debug 30"), SID("DDGI Probe Debug 31")};

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

    const uint32_t residentCap = params.bCascadeSampling ? DDGI_MAX_RESIDENT_LOCAL_VOLUMES / 2u : DDGI_MAX_RESIDENT_LOCAL_VOLUMES;
    const uint32_t maxResident = glm::min(residentCap, DDGI_MAX_VOLUME_SLOTS - cascades.count);
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

        // Least-warmed slot wins the one update per frame
        uint32_t updatedLocalSlot = localBase;
        uint32_t bestWarmup = UINT32_MAX;
        for (uint32_t s = 0; s < selectedCount; ++s) {
            const uint32_t k = localBase + (static_cast<uint32_t>(frameNumber) + s) % selectedCount;
            if (cascades.localWarmup[k] < bestWarmup) {
                bestWarmup = cascades.localWarmup[k];
                updatedLocalSlot = k;
            }
        }
        cascades.localWarmup[updatedLocalSlot] = glm::min(cascades.localWarmup[updatedLocalSlot] + 1u, DDGI_LOCAL_WARMUP_UPDATES);
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
            volume.atlasRows = DDGI_MAX_RESIDENT_LOCAL_VOLUMES;
            volume.baseCell = baseCell;

            cascades.volumes[k] = volume;
            cascades.localIds[k] = local.volumeId;
            cascades.bUpdated[k] = !bFreeze && (bColdStart || k == updatedLocalSlot);
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
    bool bValid{false};
};

struct DDGICascadeDescSources
{
    DDGICascadeDescSource entries[DDGI_MAX_VOLUME_SLOTS]{};
    uint32_t count{0};
    uint32_t localCount{0};
};

/** Small vkCmdUpdateBuffer pass resolving the sources' descriptor indices/addresses at execute time into a DDGICascadeSetGPU. Sources must outlive execution (arena-allocated). */
static void AddDDGICascadeDescriptorUpload(RenderGraph& graph, StringID passName, StringID bufferId, const DDGICascadeDescSources* sources)
{
    graph.CreateBuffer(bufferId, sizeof(DDGICascadeSetGPU), false);
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::DDGI);
    pass.WriteTransferBuffer(bufferId);
    pass.Execute([sources, bufferId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DDGICascadeSetGPU set{};
        set.cascadeCount = sources->count;
        set.localCount = sources->localCount;
        for (uint32_t k = 0; k < sources->count + sources->localCount; ++k) {
            const DDGICascadeDescSource& source = sources->entries[k];
            DDGICascadeDescriptor& desc = set.cascades[k];
            desc.volume = source.volume;
            if (!source.bValid) {
                continue;
            }
            desc.irradianceIndex = graph.PeekSampledImageViewDescriptorIndex(source.irradiance);
            desc.visibilityIndex = graph.PeekSampledImageViewDescriptorIndex(source.visibility);
            desc.probeOffsets = source.offsets != StringID{} ? graph.PeekBufferAddress(source.offsets) : 0;
            desc.bOffsetsValid = source.offsets != StringID{} ? 1u : 0u;
            desc.bValid = 1u;
        }
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(bufferId), 0, sizeof(set), &set);
    });
}

void SetupDDGIProbeUpdate(RenderGraph& graph, PipelineManager* pipelineManager, Core::Arena& arena, const Core::DDGIParams& params, const DDGICascades& cascades, const DDGICascades& previous, int32_t skyboxIndex, float iblIntensity, uint64_t frameNumber, bool bBounceOnly, const RadianceCacheFrame& radianceCache, uint32_t reflectionProbeCount, bool bReflectionProbeBruteForce)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER) || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER)) {
        return;
    }
    if (cascades.count == 0) {
        return;
    }

    const uint32_t total = cascades.count + cascades.localCount;
    const uint32_t prevTotal = previous.count + previous.localCount;

    const bool bClassify = params.bClassification && params.bRelocation;

    bool bHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bOffsetsHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bRestartHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    bool bActiveHistoryValid[DDGI_MAX_VOLUME_SLOTS]{};
    for (uint32_t k = 0; k < total; ++k) {
        const bool bSameWindow = k < prevTotal && previous.localIds[k] == cascades.localIds[k] && previous.volumes[k].probeCount == cascades.volumes[k].probeCount
            && previous.volumes[k].probeSpacing == cascades.volumes[k].probeSpacing && previous.volumes[k].irradianceGamma == cascades.volumes[k].irradianceGamma;
        bHistoryValid[k] = bSameWindow && (k >= cascades.count
                                               ? graph.HasTexture(DDGI_LOCAL_IRRADIANCE) && graph.HasTexture(DDGI_LOCAL_VISIBILITY)
                                               : graph.HasTexture(DDGI_IRRADIANCE_HISTORY[k]) && graph.HasTexture(DDGI_VISIBILITY_HISTORY[k]));
        bOffsetsHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(DDGI_OFFSETS_HISTORY[k]);
        bRestartHistoryValid[k] = bSameWindow && params.bRelocation && graph.HasBuffer(DDGI_RESTART_HISTORY[k]);
        bActiveHistoryValid[k] = bSameWindow && bClassify && graph.HasBuffer(DDGI_ACTIVE_HISTORY[k]);
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
                    .irradiance = k >= cascades.count ? DDGI_LOCAL_IRRADIANCE : DDGI_IRRADIANCE_HISTORY[k],
                    .visibility = k >= cascades.count ? DDGI_LOCAL_VISIBILITY : DDGI_VISIBILITY_HISTORY[k],
                    .offsets = bOffsetsHistoryValid[k] ? DDGI_OFFSETS_HISTORY[k] : StringID{},
                    .bValid = true,
                };
            } else {
                prevSources->entries[k].volume = k < prevTotal ? previous.volumes[k] : cascades.volumes[k];
            }
        }
        AddDDGICascadeDescriptorUpload(graph, SID("DDGI Prev Cascade Descriptors"), DDGI_CASCADES_PREV_BUFFER, prevSources);
    }

    if (cascades.localCount > 0) {
        const uint32_t localProbes = static_cast<uint32_t>(Core::LOCAL_DDGI_PROBES_PER_AXIS);
        constexpr VkImageUsageFlags sharedUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        graph.CreateTexture(DDGI_LOCAL_IRRADIANCE, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, localProbes * localProbes * DDGI_IRRADIANCE_TILE, localProbes * DDGI_IRRADIANCE_TILE * DDGI_MAX_RESIDENT_LOCAL_VOLUMES, 1}, {std::nullopt}, false);
        graph.CreateTexture(DDGI_LOCAL_VISIBILITY, TextureInfo{VK_FORMAT_R16G16_SFLOAT, localProbes * localProbes * DDGI_VISIBILITY_TILE, localProbes * DDGI_VISIBILITY_TILE * DDGI_MAX_RESIDENT_LOCAL_VOLUMES, 1}, {std::nullopt}, false);
        graph.CarryTextureToNextFrame(DDGI_LOCAL_IRRADIANCE, DDGI_LOCAL_IRRADIANCE, sharedUsage);
        graph.CarryTextureToNextFrame(DDGI_LOCAL_VISIBILITY, DDGI_LOCAL_VISIBILITY, sharedUsage);
    }

    for (uint32_t k = 0; k < total; ++k) {
        const DDGIVolumeParams& volume = cascades.volumes[k];
        const bool bLocal = k >= cascades.count;
        const uint32_t probeCountTotal = volume.probeCount.x * volume.probeCount.y * volume.probeCount.z;

        const StringID irradianceId = bLocal ? DDGI_LOCAL_IRRADIANCE : DDGI_IRRADIANCE[k];
        const StringID visibilityId = bLocal ? DDGI_LOCAL_VISIBILITY : DDGI_VISIBILITY[k];
        const StringID irradianceHistoryId = bLocal ? DDGI_LOCAL_IRRADIANCE : DDGI_IRRADIANCE_HISTORY[k];
        const StringID visibilityHistoryId = bLocal ? DDGI_LOCAL_VISIBILITY : DDGI_VISIBILITY_HISTORY[k];

        if (!cascades.bUpdated[k]) {
            if (bHistoryValid[k] && !bLocal) {
                graph.CarryTextureToNextFrame(DDGI_IRRADIANCE_HISTORY[k], DDGI_IRRADIANCE_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
                graph.CarryTextureToNextFrame(DDGI_VISIBILITY_HISTORY[k], DDGI_VISIBILITY_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
            }
            if (bOffsetsHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_OFFSETS_HISTORY[k], DDGI_OFFSETS_HISTORY[k], 0);
            }
            if (bRestartHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_RESTART_HISTORY[k], DDGI_RESTART_HISTORY[k], 0);
            }
            if (bActiveHistoryValid[k]) {
                graph.CarryBufferToNextFrame(DDGI_ACTIVE_HISTORY[k], DDGI_ACTIVE_HISTORY[k], 0);
            }
            continue;
        }

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
            if (graph.HasTexture(DDGI_LOCAL_IRRADIANCE) && graph.HasTexture(DDGI_LOCAL_VISIBILITY)) {
                tracePass.ReadSampledImage(DDGI_LOCAL_IRRADIANCE);
                tracePass.ReadSampledImage(DDGI_LOCAL_VISIBILITY);
            }
            for (uint32_t j = 0; j < total; ++j) {
                if (bHistoryValid[j] && j < cascades.count) {
                    tracePass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[j]);
                    tracePass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[j]);
                }
                if (bOffsetsHistoryValid[j]) {
                    tracePass.ReadBuffer(DDGI_OFFSETS_HISTORY[j]);
                }
            }
        } else if (bOffsetsHistoryValid[k]) {
            tracePass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            tracePass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        tracePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, skyboxIndex, iblIntensity, raysPerProbe, probeCountTotal, bBounceOnly, bFeedback, bWorldGrid, maxRayRadiance, bounceIntensity, radianceCacheShadeInterval, reflectionProbeCount, bReflectionProbeBruteForce, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], offsetsHistoryId = DDGI_OFFSETS_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], rayDataId = DDGI_RAY_DATA[k], frameNumber, bRadianceCache = radianceCache.bValid](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .probeOffsets = bOffsetsHistory ? graph.GetBufferAddress(offsetsHistoryId) : 0,
                .previousCascades = bFeedback ? graph.GetBufferAddress(DDGI_CASCADES_PREV_BUFFER) : 0,
                .radianceCache = bRadianceCache ? graph.GetBufferAddress(RADIANCE_CACHE_BUFFERS_CURRENT) : 0,
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
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

        if (!bLocal) {
            const uint32_t atlasWidth = volume.probeCount.x * volume.probeCount.y * DDGI_IRRADIANCE_TILE;
            const uint32_t atlasHeight = volume.probeCount.z * DDGI_IRRADIANCE_TILE;
            graph.CreateTexture(DDGI_IRRADIANCE[k], TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, atlasWidth, atlasHeight, 1}, {std::nullopt}, false);
            graph.CreateTexture(DDGI_VISIBILITY[k], TextureInfo{VK_FORMAT_R16G16_SFLOAT, volume.probeCount.x * volume.probeCount.y * DDGI_VISIBILITY_TILE, volume.probeCount.z * DDGI_VISIBILITY_TILE, 1}, {std::nullopt}, false);
        }

        RenderPass& blendPass = graph.AddPass(DDGI_BLEND_IRRADIANCE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        blendPass.ReadBuffer(DDGI_RAY_DATA[k]);
        blendPass.WriteStorageImage(irradianceId);
        // In place: the storage view carries the history, so declaring a sampled read of the same image would demand two layouts at once.
        if (bHistoryValid[k] && !bLocal) {
            blendPass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[k]);
        }
        if (bRestartHistoryValid[k]) {
            blendPass.ReadBuffer(DDGI_RESTART_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            blendPass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        blendPass.Execute([pipelineManager, hysteresis = glm::clamp(params.hysteresis, 0.0f, 0.995f), irradianceThreshold = params.irradianceThreshold, brightnessThreshold = params.brightnessThreshold, volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = DDGI_RAY_DATA[k], restartHistoryId = DDGI_RESTART_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], atlasId = irradianceId, atlasHistoryId = irradianceHistoryId, bInPlace = bLocal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(restartHistoryId) : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .atlasHistoryIndex = bHistory && !bInPlace ? graph.GetSampledImageViewDescriptorIndex(atlasHistoryId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = hysteresis,
                .irradianceThreshold = irradianceThreshold,
                .brightnessThreshold = brightnessThreshold,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
                .bHistoryInPlace = bInPlace ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        RenderPass& visibilityPass = graph.AddPass(DDGI_BLEND_VISIBILITY_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
        visibilityPass.ReadBuffer(DDGI_RAY_DATA[k]);
        visibilityPass.WriteStorageImage(visibilityId);
        if (bHistoryValid[k] && !bLocal) {
            visibilityPass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[k]);
        }
        if (bRestartHistoryValid[k]) {
            visibilityPass.ReadBuffer(DDGI_RESTART_HISTORY[k]);
        }
        if (bActiveHistoryValid[k]) {
            visibilityPass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
        }
        visibilityPass.Execute([pipelineManager, visibilityHysteresis = glm::clamp(params.visibilityHysteresis, 0.0f, 0.995f), distanceExponent = glm::max(params.distanceExponent, 1.0f), volume, rayRotation, previousBaseCell, bHistory = bHistoryValid[k], bRestartHistory = bRestartHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], raysPerProbe, probeCountTotal, rayDataId = DDGI_RAY_DATA[k], restartHistoryId = DDGI_RESTART_HISTORY[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], atlasId = visibilityId, atlasHistoryId = visibilityHistoryId, bInPlace = bLocal](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .probeRestart = bRestartHistory ? graph.GetBufferAddress(restartHistoryId) : 0,
                .atlasOutIndex = graph.GetStorageImageViewDescriptorIndex(atlasId),
                .atlasHistoryIndex = bHistory && !bInPlace ? graph.GetSampledImageViewDescriptorIndex(atlasHistoryId) : 0u,
                .raysPerProbe = raysPerProbe,
                .hysteresis = visibilityHysteresis,
                .distanceExponent = distanceExponent,
                .bRestartValid = bRestartHistory ? 1u : 0u,
                .probeActive = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                .bActiveValid = bActiveHistory ? 1u : 0u,
                .bHistoryInPlace = bInPlace ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, probeCountTotal, 1, 1);
        });

        if (params.bRelocation) {
            // World-space standoff scales with the cascade like the biases, so coarse probes keep proportionate clearance; locals are finest and stay unscaled.
            const float minFrontfaceDistance = glm::max(params.minFrontfaceDistance, 0.0f) * (bLocal ? 1.0f : static_cast<float>(1u << k));
            graph.CreateBuffer(DDGI_OFFSETS[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(glm::vec4), false);
            graph.CreateBuffer(DDGI_RESTART[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(uint32_t), false);

            RenderPass& relocatePass = graph.AddPass(DDGI_RELOCATE_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::DDGI);
            relocatePass.ReadBuffer(DDGI_RAY_DATA[k]);
            relocatePass.WriteBuffer(DDGI_OFFSETS[k]);
            relocatePass.WriteBuffer(DDGI_RESTART[k]);
            if (bOffsetsHistoryValid[k]) {
                relocatePass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
            }
            if (bClassify) {
                graph.CreateBuffer(DDGI_ACTIVE[k], static_cast<VkDeviceSize>(probeCountTotal) * sizeof(uint32_t), false);
                relocatePass.WriteBuffer(DDGI_ACTIVE[k]);
            }
            if (bActiveHistoryValid[k]) {
                relocatePass.ReadBuffer(DDGI_ACTIVE_HISTORY[k]);
            }
            relocatePass.Execute([pipelineManager, volume, rayRotation, previousBaseCell, bOffsetsHistory = bOffsetsHistoryValid[k], bActiveHistory = bActiveHistoryValid[k], bClassify, raysPerProbe, probeCountTotal, minFrontfaceDistance, rayDataId = DDGI_RAY_DATA[k], offsetsHistoryId = DDGI_OFFSETS_HISTORY[k], offsetsId = DDGI_OFFSETS[k], restartId = DDGI_RESTART[k], activeHistoryId = DDGI_ACTIVE_HISTORY[k], activeId = DDGI_ACTIVE[k]](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_relocate"));
                if (!pipelineEntry) {
                    return;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                DDGIProbeRelocatePushConstant pc{
                    .volume = volume,
                    .rayRotation = rayRotation,
                    .previousBaseCell = previousBaseCell,
                    .bOffsetsValid = bOffsetsHistory ? 1u : 0u,
                    .rayData = graph.GetBufferAddress(rayDataId),
                    .offsetsIn = bOffsetsHistory ? graph.GetBufferAddress(offsetsHistoryId) : 0,
                    .offsetsOut = graph.GetBufferAddress(offsetsId),
                    .restartOut = graph.GetBufferAddress(restartId),
                    .raysPerProbe = raysPerProbe,
                    .minFrontfaceDistance = minFrontfaceDistance,
                    .activeIn = bActiveHistory ? graph.GetBufferAddress(activeHistoryId) : 0,
                    .activeOut = bClassify ? graph.GetBufferAddress(activeId) : 0,
                    .bActiveValid = bActiveHistory ? 1u : 0u,
                    .bClassify = bClassify ? 1u : 0u,
                };
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (probeCountTotal + 63) / 64, 1, 1);
            });

            graph.CarryBufferToNextFrame(DDGI_OFFSETS[k], DDGI_OFFSETS_HISTORY[k], 0);
            graph.CarryBufferToNextFrame(DDGI_RESTART[k], DDGI_RESTART_HISTORY[k], 0);
            if (bClassify) {
                graph.CarryBufferToNextFrame(DDGI_ACTIVE[k], DDGI_ACTIVE_HISTORY[k], 0);
            }
        }

        if (!bLocal) {
            graph.CarryTextureToNextFrame(DDGI_IRRADIANCE[k], DDGI_IRRADIANCE_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
            graph.CarryTextureToNextFrame(DDGI_VISIBILITY[k], DDGI_VISIBILITY_HISTORY[k], VK_IMAGE_USAGE_SAMPLED_BIT);
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
                .irradiance = DDGI_LOCAL_IRRADIANCE,
                .visibility = DDGI_LOCAL_VISIBILITY,
                .offsets = params.bRelocation && cascades.bUpdated[k] ? DDGI_OFFSETS[k] : (bOffsetsHistoryValid[k] ? DDGI_OFFSETS_HISTORY[k] : StringID{}),
                .bValid = cascades.bUpdated[k] || bHistoryValid[k],
            };
        } else if (cascades.bUpdated[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = DDGI_IRRADIANCE[k],
                .visibility = DDGI_VISIBILITY[k],
                .offsets = params.bRelocation ? DDGI_OFFSETS[k] : StringID{},
                .bValid = true,
            };
        } else if (bHistoryValid[k]) {
            sources->entries[k] = DDGICascadeDescSource{
                .volume = cascades.volumes[k],
                .irradiance = DDGI_IRRADIANCE_HISTORY[k],
                .visibility = DDGI_VISIBILITY_HISTORY[k],
                .offsets = bOffsetsHistoryValid[k] ? DDGI_OFFSETS_HISTORY[k] : StringID{},
                .bValid = true,
            };
        } else {
            sources->entries[k].volume = cascades.volumes[k];
        }
    }
    AddDDGICascadeDescriptorUpload(graph, SID("DDGI Cascade Descriptors"), DDGI_CASCADES_BUFFER, sources);
}

bool AddDDGISampleDependencies(RenderGraph& graph, RenderPass& pass)
{
    if (!graph.HasBuffer(DDGI_CASCADES_BUFFER)) {
        return false;
    }
    pass.ReadBuffer(DDGI_CASCADES_BUFFER);

    if (graph.HasTexture(DDGI_LOCAL_IRRADIANCE) && graph.HasTexture(DDGI_LOCAL_VISIBILITY)) {
        pass.ReadSampledImage(DDGI_LOCAL_IRRADIANCE);
        pass.ReadSampledImage(DDGI_LOCAL_VISIBILITY);
    }

    for (uint32_t k = 0; k < DDGI_MAX_VOLUME_SLOTS; ++k) {
        if (graph.HasTexture(DDGI_IRRADIANCE[k]) && graph.HasTexture(DDGI_VISIBILITY[k])) {
            pass.ReadSampledImage(DDGI_IRRADIANCE[k]);
            pass.ReadSampledImage(DDGI_VISIBILITY[k]);
        }
        if (graph.HasTexture(DDGI_IRRADIANCE_HISTORY[k]) && graph.HasTexture(DDGI_VISIBILITY_HISTORY[k])) {
            pass.ReadSampledImage(DDGI_IRRADIANCE_HISTORY[k]);
            pass.ReadSampledImage(DDGI_VISIBILITY_HISTORY[k]);
        }
        if (graph.HasBuffer(DDGI_OFFSETS[k])) {
            pass.ReadBuffer(DDGI_OFFSETS[k]);
        }
        if (graph.HasBuffer(DDGI_OFFSETS_HISTORY[k])) {
            pass.ReadBuffer(DDGI_OFFSETS_HISTORY[k]);
        }
    }
    return true;
}

// Cascade identification tints for the all-cascades debug view (unorm RGBA, low byte = red): white, warm red, green, blue.
static const uint32_t DDGI_CASCADE_TINT[DDGI_MAX_VOLUME_SLOTS] = {
    0xFFFFFFFFu, 0xFF8C8CFFu, 0xFF8CFF8Cu, 0xFFFFB399u,
    0xFF99FFFFu, 0xFFFF99FFu, 0xFFFFFF99u, 0xFFB399FFu,
    0xFF99B3FFu, 0xFFB3FF99u, 0xFF99FFB3u, 0xFFFF99B3u,
    0xFFCCCCCCu, 0xFFCC99FFu, 0xFF99CCFFu, 0xFFFFCC99u,
    0xFFCCFF99u, 0xFF99FFCCu, 0xFFFF99CCu, 0xFFCCCCFFu,
    0xFFFFCCCCu, 0xFFCCFFCCu, 0xFFB3B3FFu, 0xFFFFB3B3u,
    0xFFB3FFB3u, 0xFFE6CCFFu, 0xFFCCE6FFu, 0xFFFFE6CCu,
    0xFFE6FFCCu, 0xFFCCFFE6u, 0xFFFFCCE6u, 0xFFE6E6E6u,
};

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGICascades& cascades, float probeDebugExposure, int32_t debugCascade, bool bHideInactive, int32_t probeDebugMode)
{
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
        const StringID atlasId = bLocal ? DDGI_LOCAL_IRRADIANCE : (graph.HasTexture(DDGI_IRRADIANCE[k]) ? DDGI_IRRADIANCE[k] : DDGI_IRRADIANCE_HISTORY[k]);
        if (!graph.HasTexture(atlasId)) {
            continue;
        }
        const StringID visibilityId = bLocal ? DDGI_LOCAL_VISIBILITY : (graph.HasTexture(DDGI_VISIBILITY[k]) ? DDGI_VISIBILITY[k] : DDGI_VISIBILITY_HISTORY[k]);
        const bool bVisibility = graph.HasTexture(visibilityId);
        const StringID offsetsId = graph.HasBuffer(DDGI_OFFSETS[k]) ? DDGI_OFFSETS[k] : DDGI_OFFSETS_HISTORY[k];
        const bool bOffsets = graph.HasBuffer(offsetsId);
        const StringID activeId = graph.HasBuffer(DDGI_ACTIVE[k]) ? DDGI_ACTIVE[k] : DDGI_ACTIVE_HISTORY[k];
        const bool bActive = graph.HasBuffer(activeId);
        const DDGIVolumeParams& volume = cascades.volumes[k];

        RenderPass& pass = graph.AddPass(DDGI_DEBUG_PASS[k], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
        pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
        pass.ReadSampledImage(atlasId);
        if (bVisibility) {
            pass.ReadSampledImage(visibilityId);
        }
        if (bOffsets) {
            pass.ReadBuffer(offsetsId);
        }
        if (bActive) {
            pass.ReadBuffer(activeId);
        }
        const uint32_t packedTint = debugCascade < 0 ? DDGI_CASCADE_TINT[k] : 0xFFFFFFFFu;
        pass.Execute([pipelineManager, volume, bOffsets, bActive, bHideInactive, probeDebugExposure, packedTint, atlasId, visibilityId, bVisibility, probeDebugMode, offsetsId, activeId](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_debug"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            DDGIProbeDebugPushConstant pc{
                .volume = volume,
                .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
                .sphereBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
                .probeOffsets = bOffsets ? graph.GetBufferAddress(offsetsId) : 0,
                .irradianceAtlasIndex = graph.GetSampledImageViewDescriptorIndex(atlasId),
                .bOffsetsValid = bOffsets ? 1u : 0u,
                .probeDebugExposure = probeDebugExposure,
                .packedTint = packedTint,
                .probeActive = bActive ? graph.GetBufferAddress(activeId) : 0,
                .bActiveValid = bActive ? 1u : 0u,
                .bHideInactive = bHideInactive ? 1u : 0u,
                .visibilityAtlasIndex = bVisibility ? graph.GetSampledImageViewDescriptorIndex(visibilityId) : 0u,
                .debugMode = probeDebugMode == 2 ? 2u : (probeDebugMode == 1 && bVisibility ? 1u : 0u),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
        });
    }
#endif
}
} // Render
