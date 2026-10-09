//
// Created by William on 2026-10-06.
//

#ifndef WILL_ENGINE_FRAME_OUTPUTS_H
#define WILL_ENGINE_FRAME_OUTPUTS_H

#include <volk.h>

#include "core/containers/array.h"
#include "core/types/extent.h"
#include "render/render-graph/render_graph_handles.h"
#include "render/shaders/ddgi_interop.h"

namespace Render
{
/** Scene-wide inputs, declared before any pass records and never reassigned, so Execute lambdas may hold a pointer to it. */
struct SceneResources
{
    RDGBuffer sceneData;
    RDGBuffer lightData;
    RDGBuffer emissiveTriWork;
    RDGBuffer reflectionProbes;

    RDGBuffer models;
    RDGBuffer instances;
    RDGBuffer materials;
    RDGBuffer primitives;
    RDGBuffer vertexPositions;
    RDGBuffer vertexAttributes;
    RDGBuffer indices;
    RDGBuffer meshlets;
    RDGBuffer meshletVertices;
    RDGBuffer meshletTriangles;
    RDGBuffer shadingBucketingDispatches;
    RDGBuffer lightingBucketingDispatches;

    RDGBuffer fontCurves;
    RDGBuffer textGlyphQuads;
    RDGBuffer textInstances;
    RDGBuffer textMaterials;
    RDGBuffer sprites;
    RDGBuffer uiGlyphQuads;

    RDGBuffer readback;
    RDGBuffer luminance;
    RDGTexture dummyBlackRG32;
#if WILL_EDITOR
    RDGBuffer debugReadback;
#endif

    // Set by SetupTLASBuild; invalid without a scene. tlasHistory is last frame's TLAS.
    RDGBuffer tlas;
    RDGBuffer tlasHistory;
};

/** Visibility bucketing output. */
struct GeometryFrame
{
    RDGBuffer lightingTileList;
};

/** SetupWorldGridBinningPass output. */
struct WorldGridFrame
{
    RDGBuffer lightGrid;
    RDGBuffer indexList;
    RDGBuffer probeGrid;
    RDGBuffer emissiveGrid;
    RDGBuffer emissiveIndexList;
    RDGBuffer cellPower;
    RDGBuffer ddgiGrid;
    RDGBuffer ddgiIndexList;

    [[nodiscard]] bool IsValid() const { return lightGrid.IsValid(); }
};

/** SetupGPUDebugBegin output; the producers of GPU debug draws append into these. */
struct GPUDebugFrame
{
    RDGBuffer sphereArgs;
    RDGBuffer sphereInstances;
    RDGBuffer cubeArgs;
    RDGBuffer cubeInstances;

    [[nodiscard]] bool IsValid() const { return sphereArgs.IsValid(); }
};

/** Radiance cache, from SetupRadianceCacheBegin and filled further by SetupRadianceCacheShade. The producer may add fields used only inside its group. */
struct RadianceCacheFrame
{
    RDGBuffer entries;
    RDGBuffer keys;
    RDGBuffer cells;
    RDGBuffer touchEntries;
    RDGBuffer active;
    RDGBuffer activeList;
    RDGBuffer activeCount;
    RDGBuffer descriptors;
    RDGBuffer buffersCurrent;
    RDGBuffer shadeArgs;
    RDGBuffer stats;

    [[nodiscard]] bool IsValid() const { return entries.IsValid(); }
};

/** DDGI probe update output. Slots without a resident volume are invalid. Grid handles are copies of the world grid's so sampling passes need only this. */
struct DDGIFrame
{
    RDGBuffer cascades;
    RDGBuffer probeOffsets;
    RDGBuffer probeActive;
    Core::Array<RDGTexture, DDGI_MAX_VOLUME_SLOTS> irradiance{};
    Core::Array<RDGTexture, DDGI_MAX_VOLUME_SLOTS> visibility{};
    RDGBuffer ddgiGrid;
    RDGBuffer ddgiIndexList;

    [[nodiscard]] bool IsValid() const { return cascades.IsValid(); }
};

/** GTAO output. */
struct GTAOFrame
{
    RDGTexture bentNormals;
    RDGTexture filtered;

    [[nodiscard]] bool IsValid() const { return filtered.IsValid(); }
};

/** Screen-space diffuse gather output. */
struct FinalGatherFrame
{
    RDGTexture data;
    RDGTexture resolved;
    RDGTexture skyVisHistory;

    [[nodiscard]] bool IsValid() const { return resolved.IsValid(); }
};

/** Reflection trace/shade output. */
struct ReflectionFrame
{
    RDGBuffer hitDescriptors;
    RDGTexture specNoisy;
    RDGTexture hitDelta;
    RDGTexture hitDeltaHistory;

    [[nodiscard]] bool IsValid() const { return specNoisy.IsValid(); }
};

/** ReSTIR DI output. Fields are invalid when their stage did not run (ReGIR off, no temporal reuse, no confidence). */
struct ReSTIRFrame
{
    RDGBuffer lightsVS;
    RDGBuffer regirHashEntries;
    RDGBuffer regirEntries;
    RDGBuffer regirCellData;
    RDGBuffer reservoirBase;
    RDGBuffer reservoirTemporal;
    RDGBuffer reservoirSpatial;
    RDGBuffer reservoirFinal;
    RDGBuffer reservoirHistory;
    RDGTexture confidence;
    RDGTexture confidenceHistory;
    RDGTexture sunVis;

    [[nodiscard]] bool IsValid() const { return reservoirFinal.IsValid(); }
};

/** RT sun shadow and its SIGMA denoise. */
struct LocalShadowFrame
{
    RDGBuffer data;
    RDGTexture atlas;

    [[nodiscard]] bool IsValid() const { return data.IsValid(); }
};

struct SunShadowFrame
{
    RDGTexture shadow;
    RDGTexture depth;
    RDGTexture gbuffer;
    RDGTexture sigmaShadow;
    RDGTexture sigmaStabilized;
    RDGTexture contact;
    Core::Extent2D extent{};
    uint32_t pixelScale{1};

    [[nodiscard]] bool IsValid() const { return shadow.IsValid(); }
};
} // Render

#endif //WILL_ENGINE_FRAME_OUTPUTS_H
