//
// Created by William on 2025-12-28.
//

#ifndef WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
#define WILL_ENGINE_PUSH_CONSTANT_INTEROP_H

#ifdef __SLANG__
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>

import common_interop;
import model_interop;
import constants_interop;
import instancing_interop;
import lights_interop;
import text_interop;
import ui_interop;
import restir_interop;
import relax_interop;
import reblur_interop;
import ddgi_interop;
import reflection_interop;
import reflection_probe_interop;
import world_grid_interop;
import radiance_cache_interop;
#else
#include <glm/glm.hpp>
#include <volk.h>
#include <cstdint>
#include "common_interop.h"
#include "model_interop.h"
#include "instancing_interop.h"
#include "text_interop.h"
#include "ui_interop.h"
#include "restir_interop.h"
#include "relax_interop.h"
#include "reblur_interop.h"
#include "ddgi_interop.h"
#include "reflection_interop.h"
#include "reflection_probe_interop.h"
#include "world_grid_interop.h"
#include "radiance_cache_interop.h"

using uint = uint32_t;
using int32 = int32_t;
using uint32 = uint32_t;

using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using float2x2 = glm::mat2;
using float3x3 = glm::mat3;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#define SHADER_CONST constexpr inline
#define SHADER_PTR(T) VkDeviceAddress
#define SHADER_ATOMIC(T) T
#endif // __SLANG__

SHADER_PUBLIC struct DebugVisualizePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) vertexPosBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) reservoirBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) reservoirTemporalBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) reservoirSpatialBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) reservoirHistoryBuffer;
    SHADER_PUBLIC int2 srcExtent;
    SHADER_PUBLIC int2 dstExtent;
    SHADER_PUBLIC float nearPlane;
    SHADER_PUBLIC uint textureArrayIndex;
    SHADER_PUBLIC uint textureIndexInArray;
    SHADER_PUBLIC uint valueTransformationType;
    SHADER_PUBLIC uint outputImageIndex;
    SHADER_PUBLIC uint depthTextureIndex;
    SHADER_PUBLIC uint checkerboardField;
    SHADER_PUBLIC uint historyCheckerboardField;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC uint reflectionProbeCount;
    SHADER_PUBLIC uint pad0;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
};

SHADER_PUBLIC struct InstanceLODPushConstant
{
    // Read-Only
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;

    // Write
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;

    // Read-Only
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC int32_t lodBias;
};

SHADER_PUBLIC struct PrefixSumUpsweep1PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;

    // Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) level1Sums;
    SHADER_PUBLIC SHADER_PTR(uint32_t) level1BlockSums;

    // Read-Only
    SHADER_PUBLIC uint32_t elementCount;
    SHADER_PUBLIC uint32_t blockCount;
};

SHADER_PUBLIC struct PrefixSumUpsweep2PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(uint32_t) level1BlockSums;

    // Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) level2Sums;
    SHADER_PUBLIC SHADER_PTR(uint32_t) level2BlockSums;

    // Read-Only
    SHADER_PUBLIC uint32_t elementCount;
    SHADER_PUBLIC uint32_t blockCount;
};

SHADER_PUBLIC struct PrefixSumScanBlocksPushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(uint32_t) level2BlockSums;

    // Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) scannedLevel2BlockSums;

    // Read-Only
    SHADER_PUBLIC uint32_t blockCount;
};

SHADER_PUBLIC struct PrefixSumDownsweep1PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(uint32_t) scannedLevel2BlockSums;

    // Read-Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) level2Sums;

    // Read-Only
    SHADER_PUBLIC uint32_t elementCount;
};

SHADER_PUBLIC struct PrefixSumDownsweep2PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(uint32_t) level1Sums;
    SHADER_PUBLIC SHADER_PTR(uint32_t) level2Sums;

    // Write
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;

    // Read-Only
    SHADER_PUBLIC uint32_t elementCount;
};

SHADER_PUBLIC struct TotalMeshletCountPushConstant
{
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer;
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;

    SHADER_PUBLIC uint32_t instanceCount;
};

SHADER_PUBLIC struct ExpandMeshletsPushConstant
{
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer; // for totalMeshletCount
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;
    SHADER_PUBLIC SHADER_PTR(IntermediateMeshlet) intermediateMeshlets;

    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;

    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t currentFrameBufferMeshletLimit;
};

SHADER_PUBLIC struct MeshletVisibilityPrefixSumUpsweep1PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(IntermediateMeshlet) intermediateMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer; // for totalMeshlets

    // Write
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletLevel1Sums;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletLevel1BlockSums;

    // Read-Only
    SHADER_PUBLIC uint32_t blockCount;
    SHADER_PUBLIC uint32_t currentFrameBufferMeshletLimit;
};

SHADER_PUBLIC struct MeshletVisibilityPrefixSumDownsweep2PushConstant
{
    // Read
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletLevel1Sums;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletLevel2Sums;
    SHADER_PUBLIC SHADER_PTR(IntermediateMeshlet) intermediateMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer; // for elementCount (totalMeshlets)

    // Write
    SHADER_PUBLIC SHADER_PTR(CompactedMeshlet) visibleMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingCompactedMeshletDispatchIndirect) compactedDispatchBuffer;

    SHADER_PUBLIC uint32_t currentFrameBufferMeshletLimit;
};

SHADER_PUBLIC struct CompactedMeshletDispatchPushConstant
{
    SHADER_PUBLIC SHADER_PTR(InstancingCompactedMeshletDispatchIndirect) compactedDispatchBuffer;

    // We do a readback that updates the meshlet intermediate/final buffer, so there is a 3-frame delay for expansion
    SHADER_PUBLIC uint32_t currentFrameBufferMeshletLimit;
};

SHADER_PUBLIC struct MaxMeshletCountPushConstant
{
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) currentHighest;
};

SHADER_PUBLIC struct VisibilityBufferAccumulatePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) vertexPosBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(CompactedMeshlet) visibleMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingCompactedMeshletDispatchIndirect) compactedDispatchBuffer; // for "total visible meshlets"
};

SHADER_PUBLIC struct VisibilityBufferResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) vertexPosBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC uint2 extents;
    // In
    SHADER_PUBLIC uint32_t visibilityBufferIndex;
    // Out
    SHADER_PUBLIC uint32_t barycentricTargetIndex;
    SHADER_PUBLIC uint32_t derivativeTargetIndex;
};


SHADER_PUBLIC struct ShadeBucketingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(ShadeDispatchParameters) shadeDispatchBuffer; // out
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer; // out
    SHADER_PUBLIC uint2 extents;
    // In
    SHADER_PUBLIC uint32_t visibilityBufferIndex;
};

SHADER_PUBLIC struct ShadeBucketingResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(ShadeDispatchParameters) shadeDispatchBuffer; // in/out
    SHADER_PUBLIC uint32_t materialCount;
};

SHADER_PUBLIC struct LightingBucketingResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer; // in/out
    SHADER_PUBLIC uint32_t lightingCount;
};

SHADER_PUBLIC struct BucketDispatchCountPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ShadeDispatchParameters) shadeDispatchBuffer; // in
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer; // in
    SHADER_PUBLIC SHADER_PTR(uint32_t) countBuffer; // out: [shadingActive, lightingActive]
    SHADER_PUBLIC uint32_t materialCount;
    SHADER_PUBLIC uint32_t lightingCount;
};

SHADER_PUBLIC struct LightingBucketVisualizePushConstant
{
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer; // in
    SHADER_PUBLIC uint32_t lightingIndex;
    // Out
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
};

SHADER_PUBLIC struct VisibilityShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) vertexPosBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(ShadeDispatchParameters) shadeDispatchBuffer; // in
    uint32_t pad0;
    uint32_t pad1;
    SHADER_PUBLIC uint2 extents;
    // In
    SHADER_PUBLIC uint32_t materialIndex;
    SHADER_PUBLIC uint32_t visibilityBufferIndex;
    SHADER_PUBLIC uint32_t barycentricBufferIndex;
    SHADER_PUBLIC uint32_t derivativeBufferIndex;
    // Out
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
};

SHADER_PUBLIC struct ShadowsResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC int32_t gtaoFilteredIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
};

SHADER_PUBLIC struct ReSTIRTransformLightsPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightVSData) lightVS;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct ReGIRTouchPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(uint) hashEntries;
    SHADER_PUBLIC SHADER_PTR(int4) activeCells;
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct ReGIRBuildIndirectPushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC SHADER_PTR(uint) indirectArgs;
};

SHADER_PUBLIC struct ReGIRFillPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightVSData) lightVS;
    SHADER_PUBLIC SHADER_PTR(int4) activeCells;
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC SHADER_PTR(ReGIRReservoir) reservoirs;
    SHADER_PUBLIC SHADER_PTR(float2) cellData;
    SHADER_PUBLIC SHADER_PTR(uint) hashEntriesPrev;
    SHADER_PUBLIC SHADER_PTR(ReGIRReservoir) reservoirsPrev;
    SHADER_PUBLIC SHADER_PTR(ReGIRTileSlot) tiles;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t bHasPrev;
    SHADER_PUBLIC float wClamp;
};

SHADER_PUBLIC struct ReGIRPresampleTilesPushConstant
{
    SHADER_PUBLIC SHADER_PTR(LightAliasEntry) aliasEntries;
    SHADER_PUBLIC SHADER_PTR(ReGIRTileSlot) tiles;
    SHADER_PUBLIC uint32_t lightCount;
    SHADER_PUBLIC uint32_t frameIndex;
};

SHADER_PUBLIC struct ReSTIRDICombinedTemporalPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightVSData) lightVS;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) hashEntries;
    SHADER_PUBLIC SHADER_PTR(ReGIRReservoir) reservoirs;
    SHADER_PUBLIC SHADER_PTR(float2) cellData;
    SHADER_PUBLIC SHADER_PTR(Reservoir) historyBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) genBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) outputBuffer;
    SHADER_PUBLIC SHADER_PTR(ReflectionHitDescriptor) reflectionDescriptors;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridEmissiveGrid;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridEmissiveIndexList;
    SHADER_PUBLIC SHADER_PTR(float2) worldGridCellPower;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t prevGbufferOneIndex;
    SHADER_PUBLIC uint32_t prevDepthIndex;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t mCap;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t prevTlasIndex;
    SHADER_PUBLIC uint32_t prevShadowVisIndex;
    SHADER_PUBLIC uint32_t shadowVisIndex;
    SHADER_PUBLIC uint32_t signalIndex;
    SHADER_PUBLIC uint32_t bPermutationSampling;
    SHADER_PUBLIC float antilagStrength;
    SHADER_PUBLIC uint32_t bInitialVisibility;
    SHADER_PUBLIC uint32_t bTemporalSearch;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC float reflectionRoughnessMax;
    SHADER_PUBLIC float brdfRoughnessMax;
    SHADER_PUBLIC float finalWClamp;
    SHADER_PUBLIC float lightSpecularFromReflectionsMax;
};

SHADER_PUBLIC struct ReSTIRDISpatialPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightVSData) lightVS;
    SHADER_PUBLIC SHADER_PTR(Reservoir) inputBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) outputBuffer;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t spatialRadius;
    SHADER_PUBLIC uint32_t spatialNeighbors;
    SHADER_PUBLIC uint32_t mCap;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t passIndex;
    SHADER_PUBLIC uint32_t bAdaptiveSpatial;
    SHADER_PUBLIC float adaptiveSpatialBoost;
    SHADER_PUBLIC uint32_t adaptiveMReference;
    SHADER_PUBLIC uint32_t bValidateVisibility;
    SHADER_PUBLIC float wClamp;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC float lightSpecularFromReflectionsMax;
};

SHADER_PUBLIC struct ReSTIRDISunPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t visIndex;
    SHADER_PUBLIC uint32_t prevGbufferOneIndex;
    SHADER_PUBLIC uint32_t prevDepthIndex;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t prevTlasIndex;
    SHADER_PUBLIC uint32_t signalIndex;
    SHADER_PUBLIC uint32_t blockerIndex;
    SHADER_PUBLIC uint32_t prevBlockerIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct ReSTIRBoilingFilterPushConstant
{
    SHADER_PUBLIC SHADER_PTR(Reservoir) inputBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) outputBuffer;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC float strength;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
};

SHADER_PUBLIC struct ReSTIRConfidenceGradientPushConstant
{
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint2 gradientExtent;
    SHADER_PUBLIC uint32_t signalIndex;
    SHADER_PUBLIC uint32_t gradientIndex;
};

SHADER_PUBLIC struct ReSTIRConfidenceResolvePushConstant
{
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint2 gradientExtent;
    SHADER_PUBLIC uint32_t gradientIndex;
    SHADER_PUBLIC uint32_t prevConfidenceIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t confidenceIndex;
    SHADER_PUBLIC float confidenceStrength;
    SHADER_PUBLIC float sensitivity;
    SHADER_PUBLIC float darknessBias;
    SHADER_PUBLIC float blendFactor;
    SHADER_PUBLIC uint32_t blurRadius;
};

SHADER_PUBLIC struct VisibilityLightingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightVSData) lightVS;
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Reservoir) reservoirBuffer;
    SHADER_PUBLIC SHADER_PTR(float4) accumulationBuffer;
    SHADER_PUBLIC uint32_t visibilityBufferIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t shadowsIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t primaryOutputImageIndex;
    SHADER_PUBLIC uint32_t secondaryOutputImageIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t lightingIndex;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t accumulationCount;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC uint32_t bCheckerboardPacked;
    SHADER_PUBLIC uint32_t reflectionIndex;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC uint32_t bDDGIApply;
    SHADER_PUBLIC uint32_t bFullRateResolve;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
    SHADER_PUBLIC uint32_t giResolvedIndex;
    SHADER_PUBLIC uint32_t giDataIndex;
    SHADER_PUBLIC uint32_t giGatherMode;
    SHADER_PUBLIC float reflectionRoughnessMax;
    SHADER_PUBLIC float lightSpecularFromReflectionsMax;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC uint32_t pad2;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC uint32_t sunVisIndex;
    SHADER_PUBLIC uint32_t pad3;
};

// Unused: kept around alongside WorldGridBinningPushConstant.
SHADER_PUBLIC struct FrustumBinningPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(uint2) lightGrid;
    SHADER_PUBLIC SHADER_PTR(uint) lightIndexList;
    SHADER_PUBLIC float zNear;
    SHADER_PUBLIC float zFar;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t pad0;
};

// No zNear/zFar: cascade selection is purely camera-relative (see binning_common.slang).
SHADER_PUBLIC struct WorldGridBinningPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridEmissiveGrid;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridEmissiveIndexList;
    SHADER_PUBLIC SHADER_PTR(float2) worldGridCellPower;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t pad0;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC uint32_t pad1;
    SHADER_PUBLIC SHADER_PTR(DDGIVolumeParams) ddgiVolumes;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridDDGIGrid;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridDDGIIndexList;
    SHADER_PUBLIC uint32_t ddgiVolumeCount;
    SHADER_PUBLIC uint32_t ddgiVolumeSlotBase;
};

SHADER_PUBLIC struct WorldGridDebugPushConstant
{
    SHADER_PUBLIC SHADER_PTR(GPUDebugDrawArgs) args;
    SHADER_PUBLIC SHADER_PTR(DebugLineSegment) segmentBuffer;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t level;
    SHADER_PUBLIC uint32_t packedTint;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct RadianceCacheCarryForwardPushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint) prevEntries;
    SHADER_PUBLIC SHADER_PTR(uint2) prevKeys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) prevCells;
    SHADER_PUBLIC SHADER_PTR(uint) nextEntries;
    SHADER_PUBLIC SHADER_PTR(uint2) nextKeys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) nextCells;
    SHADER_PUBLIC float4 cameraPos;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t bFreeze;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheStats) stats;
};

SHADER_PUBLIC struct RadianceCacheBuildIndirectPushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC SHADER_PTR(uint) indirectArgs;
};

SHADER_PUBLIC struct RadianceCacheShadePushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint) activeList;
    SHADER_PUBLIC SHADER_PTR(uint) activeCount;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheHitDescriptor) descriptors;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) cells;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) previousCascades;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) indexBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) vertexPosBuffer;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t bFeedbackValid;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC float maxRadiance;
    SHADER_PUBLIC float bounceIntensity;
    SHADER_PUBLIC uint32_t accumCap;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheStats) stats;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct GIGatherPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC SHADER_PTR(uint) cacheEntries;
    SHADER_PUBLIC SHADER_PTR(uint2) cacheKeys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) cacheCells;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) indexBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC uint2 gatherExtent;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t shRIndex;
    SHADER_PUBLIC uint32_t shGIndex;
    SHADER_PUBLIC uint32_t shBIndex;
    SHADER_PUBLIC uint32_t dataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC uint32_t bCascadesValid;
    SHADER_PUBLIC uint32_t litHistoryIndex;
    SHADER_PUBLIC uint32_t depthHistoryIndex;
    SHADER_PUBLIC uint32_t gbufferOneHistoryIndex;
    SHADER_PUBLIC uint32_t bSkipRay;
    SHADER_PUBLIC uint32_t guideOutIndex;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC float bakedDiffuseClampK;
    SHADER_PUBLIC uint32_t skyVisIndex;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC uint32_t rayCount;
    SHADER_PUBLIC uint32_t giHistoryIndex;
    SHADER_PUBLIC uint32_t varGuideOutIndex;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct GIDenoisePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 gatherExtent;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t dataIndex;
    SHADER_PUBLIC uint32_t srcShRIndex;
    SHADER_PUBLIC uint32_t srcShGIndex;
    SHADER_PUBLIC uint32_t srcShBIndex;
    SHADER_PUBLIC uint32_t dstShRIndex;
    SHADER_PUBLIC uint32_t dstShGIndex;
    SHADER_PUBLIC uint32_t dstShBIndex;
    SHADER_PUBLIC uint32_t direction;
    SHADER_PUBLIC uint32_t stepSize;
    SHADER_PUBLIC uint32_t aoIndex;
    SHADER_PUBLIC float chromaLumaPower;
    SHADER_PUBLIC uint32_t srcSkyVisIndex;
    SHADER_PUBLIC uint32_t dstSkyVisIndex;
};

SHADER_PUBLIC struct GIUpscalePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 gatherExtent;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t shRIndex;
    SHADER_PUBLIC uint32_t shGIndex;
    SHADER_PUBLIC uint32_t shBIndex;
    SHADER_PUBLIC uint32_t historyIndex;
    SHADER_PUBLIC uint32_t depthHistoryIndex;
    SHADER_PUBLIC uint32_t gbufferOneHistoryIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t bHistoryValid;
    SHADER_PUBLIC uint32_t dataIndex;
    SHADER_PUBLIC uint32_t skyVisIndex;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC uint32_t bCascadesValid;
    SHADER_PUBLIC uint32_t aoIndex;
    SHADER_PUBLIC uint32_t bentNormalIndex;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC uint32_t momentsIndex;
    SHADER_PUBLIC uint32_t momentsHistoryIndex;
    SHADER_PUBLIC uint32_t bMomentsValid;
    SHADER_PUBLIC uint32_t motionTileIndex;
    SHADER_PUBLIC uint32_t varGuideIndex;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct GIMotionTileMaxPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t tileMaxIndex;
};

SHADER_PUBLIC struct ReSTIRRemodulatePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t diffuseIndex;
    SHADER_PUBLIC uint32_t specularIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t width;
    SHADER_PUBLIC uint32_t height;
    SHADER_PUBLIC uint32_t outputMode;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC float indirectIntensity;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC uint32_t bDDGIApply;
    SHADER_PUBLIC uint32_t shadowsIndex;
    SHADER_PUBLIC uint32_t reflectionIndex;
    SHADER_PUBLIC float reflectionRoughnessMax;
    SHADER_PUBLIC uint32_t giResolvedIndex;
    SHADER_PUBLIC uint32_t giDataIndex;
    SHADER_PUBLIC uint32_t giGatherMode;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    // Traced reflections were already summed into the denoised specular channel at the lighting resolve; suppress the
    // separate reflection composite (and its probe/sky replacement) inside the traced roughness range.
    SHADER_PUBLIC uint32_t bReflectionMerged;
    SHADER_PUBLIC uint32_t padR0;
};

SHADER_PUBLIC struct ReflectionTracePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ReflectionHitDescriptor) reflectionDescriptors;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC float roughnessMax;
};

SHADER_PUBLIC struct SSRTracePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ReflectionHitDescriptor) reflectionDescriptors;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC float roughnessMax;
    SHADER_PUBLIC float ssrThickness;
    SHADER_PUBLIC uint32_t ssrMaxSteps;
    SHADER_PUBLIC float edgeFade;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct ReflectionShadePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ReflectionHitDescriptor) reflectionDescriptors;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) indexBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t activeCheckerboardField;
    SHADER_PUBLIC float roughnessMax;
    SHADER_PUBLIC float intensity;
    SHADER_PUBLIC uint32_t bDDGIApply;
    SHADER_PUBLIC uint32_t bCheckerboardPacked;
    SHADER_PUBLIC uint32_t litHistoryIndex;
    SHADER_PUBLIC uint32_t depthHistoryIndex;
    SHADER_PUBLIC uint32_t gbufferOneHistoryIndex;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC uint32_t sunMode; // REFLECTION_SUN_* for the analytic hit-shading fallback
    SHADER_PUBLIC uint32_t padS0;
};

SHADER_PUBLIC struct TemporalAntialiasingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t colorResolvedIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t depthHistoryIndex;
    SHADER_PUBLIC uint32_t colorHistoryIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferOneHistoryIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
    SHADER_PUBLIC uint32_t outputCopyIndex;
    SHADER_PUBLIC float baseBlendAlpha;
    SHADER_PUBLIC float disocclusionThreshold;
    SHADER_PUBLIC float varianceGammaLuma;
    SHADER_PUBLIC float varianceGammaChroma;
    SHADER_PUBLIC float karisStrength;
    SHADER_PUBLIC float invalidHistoryBlend;
    SHADER_PUBLIC float lumaBoostCap;
    SHADER_PUBLIC float grazingTurnoverStrength;
    SHADER_PUBLIC SHADER_PTR(float) exposureLuminance;
    SHADER_PUBLIC float exposureTarget;
    SHADER_PUBLIC float pad1;
};

// Donut-ported native-res TAA resolve (shaders/donut_taa.slang); fields match the shader.
SHADER_PUBLIC struct DonutTaaPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t colorInputIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t feedbackInputIndex;
    SHADER_PUBLIC uint32_t historyClampRelaxIndex;
    SHADER_PUBLIC uint32_t colorOutputIndex;
    SHADER_PUBLIC uint32_t feedbackOutputIndex;
    SHADER_PUBLIC float clampingFactor;
    SHADER_PUBLIC float newFrameWeight;
    SHADER_PUBLIC float pqC;
    SHADER_PUBLIC float invPqC;
    SHADER_PUBLIC uint32_t useHistoryClampRelax;
    SHADER_PUBLIC uint32_t useCatmullRom;
    SHADER_PUBLIC float2 inputViewOrigin;
    SHADER_PUBLIC float2 inputViewSize;
    SHADER_PUBLIC float2 outputViewOrigin;
    SHADER_PUBLIC float2 outputViewSize;
    SHADER_PUBLIC float2 outputTextureSizeInv;
    SHADER_PUBLIC float2 inputOverOutputViewSize;
    SHADER_PUBLIC float2 outputOverInputViewSize;
};

SHADER_PUBLIC struct SmaaEdgeDetectionPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t colorIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputEdgeIndex;
    SHADER_PUBLIC float threshold;
    SHADER_PUBLIC float localContrastAdaptation;
};

SHADER_PUBLIC struct SmaaBlendWeightPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t edgeIndex;
    SHADER_PUBLIC uint32_t outputBlendIndex;
    SHADER_PUBLIC int32_t maxSearchSteps;
    SHADER_PUBLIC int32_t maxSearchStepsDiag;
};

SHADER_PUBLIC struct SmaaNeighborhoodBlendPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t colorIndex;
    SHADER_PUBLIC uint32_t blendWeightIndex;
    SHADER_PUBLIC uint32_t outputIndex;
};

SHADER_PUBLIC struct SmaaTemporalResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t currentColorIndex;
    SHADER_PUBLIC uint32_t previousColorIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t outputIndex;
};

SHADER_PUBLIC SHADER_CONST uint32_t POST_PROCESS_FINALIZE_FLAG_EXPOSURE = 1u << 0;
SHADER_PUBLIC SHADER_CONST uint32_t POST_PROCESS_FINALIZE_FLAG_BLOOM = 1u << 1;
SHADER_PUBLIC SHADER_CONST uint32_t POST_PROCESS_FINALIZE_FLAG_GRADING = 1u << 2;

// 172 bytes; relies on the Vulkan 1.4 256-byte maxPushConstantsSize guarantee
SHADER_PUBLIC struct PostProcessFinalizePushConstant
{
    // White balance * identity-saturation basis, rows of a 3x3 matrix applied post-tonemap (w unused)
    SHADER_PUBLIC float4 wbRow0;
    SHADER_PUBLIC float4 wbRow1;
    SHADER_PUBLIC float4 wbRow2;
    // Operator params, vector-packed so the layout is stride-unambiguous:
    // Hable/Reinhard: params0.x=whitePoint
    // Uchimura: params0=(P,a,m,l), params1=(c,b,_,_)
    // AgX: params0=(minEV,maxEV,_,_)
    // Khronos: params0=(startCompression,desaturation,_,_)
    SHADER_PUBLIC float4 params0;
    SHADER_PUBLIC float4 params1;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t srcImageIndex;
    SHADER_PUBLIC uint32_t dstImageIndex;
    SHADER_PUBLIC uint32_t bloomImageIndex;
    // -1=None, 0=ACES Fitted, 1=Hable, 2=Reinhard, 3=Lottes, 4=Reinhard-Jodie, 5=Clamp, 6=Hejl-Burgess-Dawson, 7=Uchimura, 8=ACES Narkowicz, 9=AgX, 10=Khronos PBR Neutral
    SHADER_PUBLIC int32_t tonemapOperator;
    SHADER_PUBLIC float targetLuminance;
    SHADER_PUBLIC float exposureBias; // exp2(colorGradingExposure), multiplied into exposure before tonemapping
    SHADER_PUBLIC float bloomIntensity; // pre-divided by bloom mip count
    SHADER_PUBLIC float contrast;
    SHADER_PUBLIC float saturation;
    SHADER_PUBLIC float vignetteStrength;
    SHADER_PUBLIC float vignetteRadius;
    SHADER_PUBLIC float vignetteSmoothness;
    SHADER_PUBLIC float vignetteRoundness;
    SHADER_PUBLIC float chromaticAberrationStrength;
    SHADER_PUBLIC float paniniStrength;
    SHADER_PUBLIC float paniniB; // stable screen-fit scale, CPU-precomputed
    SHADER_PUBLIC float paniniVerticalFocalLength;
    SHADER_PUBLIC float aspect;
    SHADER_PUBLIC uint32_t flags;
};

SHADER_PUBLIC struct PostProcessComposePushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float sharpenStrength; // 0 = off
    SHADER_PUBLIC float grainStrength; // 0 = off
    SHADER_PUBLIC float grainSize;
    SHADER_PUBLIC float grainResponse;
    SHADER_PUBLIC float ditherStrength; // 0 = off; CPU zeroes when output is rescaled before presentation
    SHADER_PUBLIC uint frameIndex;
};

SHADER_PUBLIC SHADER_CONST uint32_t SCREEN_FADE_MODE_FADE = 1u;
SHADER_PUBLIC SHADER_CONST uint32_t SCREEN_FADE_MODE_IRIS = 2u;
SHADER_PUBLIC SHADER_CONST uint32_t SCREEN_FADE_MODE_WIPE = 3u;
SHADER_PUBLIC SHADER_CONST uint32_t SCREEN_FADE_MODE_DISSOLVE = 4u;
SHADER_PUBLIC SHADER_CONST uint32_t SCREEN_FADE_MODE_LETTERBOX = 5u;

SHADER_PUBLIC struct ScreenFadePushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float2 center;
    SHADER_PUBLIC float2 direction;
    SHADER_PUBLIC float4 color; // w unused
    SHADER_PUBLIC float progress;
    SHADER_PUBLIC float softness;
    SHADER_PUBLIC float aspect;
    SHADER_PUBLIC uint32_t mode;
};

SHADER_PUBLIC struct BuildDirectIndirectPushConstant
{
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(DrawMeshTasksIndirectCommand) indirectCommandBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t lodBias;
};

SHADER_PUBLIC struct DirectMeshShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;

    SHADER_PUBLIC SHADER_PTR(VertexPosition) positionBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) attrBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;

    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;

    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct HistogramBuildPushConstant
{
    SHADER_PUBLIC uint32_t hdrImageIndex;
    SHADER_PUBLIC uint32_t _pad0;
    SHADER_PUBLIC SHADER_PTR(uint32_t) histogramBufferAddress;
    SHADER_PUBLIC uint32_t width;
    SHADER_PUBLIC uint32_t height;
    SHADER_PUBLIC float minLogLuminance;
    SHADER_PUBLIC float oneOverLogLuminanceRange;
    // Meters every other pixel per axis; offset cycles the 2x2 phase per frame
    SHADER_PUBLIC uint2 sampleOffset;
};

SHADER_PUBLIC struct ExposureCalculatePushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint32_t) histogramBufferAddress;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC float minLogLuminance;
    SHADER_PUBLIC float logLuminanceRange;
    SHADER_PUBLIC float alphaBrighten; // 1-exp2(-speed*dt), applied while adapted luminance decreases
    SHADER_PUBLIC float alphaDarken; // applied while adapted luminance increases
    SHADER_PUBLIC float lowPercentile;
    SHADER_PUBLIC float highPercentile;
    SHADER_PUBLIC float minAdaptedLuminance; // targetLuminance * exp2(-maxGainEV)
    SHADER_PUBLIC float maxAdaptedLuminance; // targetLuminance * exp2(-minGainEV)
    SHADER_PUBLIC uint32_t totalPixels;
};

SHADER_PUBLIC struct MotionBlurVelocityExtractPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 extent;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthBufferIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t bObjectOnly; // 0 keeps full camera+object motion
};

SHADER_PUBLIC struct MotionBlurTileVelocityPushConstant
{
    SHADER_PUBLIC uint2 velocityBufferSize;
    SHADER_PUBLIC uint2 tileBufferSize;
    SHADER_PUBLIC uint32_t velocityBufferIndex;
    SHADER_PUBLIC uint32_t tileMaxIndex;
    // Applied before the tile-radius clamp so the stored max describes the blur that will actually be gathered
    SHADER_PUBLIC float velocityScale;
};

SHADER_PUBLIC struct MotionBlurNeighborMaxPushConstant
{
    SHADER_PUBLIC uint2 tileBufferSize;
    SHADER_PUBLIC uint tileMaxIndex;
    SHADER_PUBLIC uint neighborMaxIndex;
};

SHADER_PUBLIC struct MotionBlurReconstructionPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 srcBufferSize;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneColorIndex;
    SHADER_PUBLIC uint32_t velocityBufferIndex;
    SHADER_PUBLIC uint32_t depthBufferIndex;
    SHADER_PUBLIC uint32_t tileNeighborMaxIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float velocityScale; // shutter fraction of inter-frame displacement
    SHADER_PUBLIC float depthScale; // 1 / soft depth-classification band in view units
};

SHADER_PUBLIC struct BloomThresholdPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint2 inputExtent;
    SHADER_PUBLIC uint32_t inputColorIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC float threshold; // display-relative, scaled into scene units by the carried exposure
    SHADER_PUBLIC float softThreshold;
    SHADER_PUBLIC float clampValue;
    SHADER_PUBLIC float targetLuminance;
    SHADER_PUBLIC uint32_t bExposureEnabled;
    SHADER_PUBLIC uint32_t _pad0;
};

SHADER_PUBLIC struct BloomDownsamplePushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint2 inputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t srcMipLevel;
};

SHADER_PUBLIC struct BloomUpsamplePushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint2 lowerExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t lowerMipLevel;
    SHADER_PUBLIC uint32_t higherMipLevel;
    SHADER_PUBLIC float radius;
};


SHADER_PUBLIC struct GTAODepthPrepassPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t inputDepth;
    SHADER_PUBLIC uint32_t outputDepth0;
    SHADER_PUBLIC uint32_t outputDepth1;
    SHADER_PUBLIC uint32_t outputDepth2;
    SHADER_PUBLIC uint32_t outputDepth3;
    SHADER_PUBLIC uint32_t outputDepth4;
    SHADER_PUBLIC float effectRadius;
    SHADER_PUBLIC float effectFalloffRange;
    SHADER_PUBLIC float radiusMultiplier;
};

SHADER_PUBLIC struct GTAOMainPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t prefilteredDepthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t aoOutputIndex;
    SHADER_PUBLIC uint32_t edgeDataIndex;

    SHADER_PUBLIC float effectRadius;
    SHADER_PUBLIC float radiusMultiplier;
    SHADER_PUBLIC float effectFalloffRange;
    SHADER_PUBLIC float sampleDistributionPower;
    SHADER_PUBLIC float thinOccluderCompensation;
    SHADER_PUBLIC float finalValuePower;
    SHADER_PUBLIC float depthMipSamplingOffset;
    SHADER_PUBLIC float sliceCount;
    SHADER_PUBLIC float stepsPerSlice;
    SHADER_PUBLIC uint32_t noiseIndex;
    SHADER_PUBLIC uint32_t bentNormalIndex;
};

SHADER_PUBLIC struct GTAODenoisePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t rawAOIndex;
    SHADER_PUBLIC uint32_t edgeDataIndex;
    SHADER_PUBLIC uint32_t filteredAOIndex;
    SHADER_PUBLIC float denoiseBlurBeta;
    SHADER_PUBLIC uint32_t isFinalDenoisePass;
};

SHADER_PUBLIC struct PortalCompositePushConstant
{
    SHADER_PUBLIC uint32_t portalColorIndex;
    SHADER_PUBLIC uint32_t portalVelocityIndex;
    SHADER_PUBLIC uint32_t portalDepthIndex;
};

SHADER_PUBLIC struct SelectionOutlinePushConstant
{
    SHADER_PUBLIC uint32_t selectedStableIdLo;
    SHADER_PUBLIC uint32_t selectedStableIdHi;
    SHADER_PUBLIC uint2 extents;
    SHADER_PUBLIC uint32_t stableIdIndex;
    SHADER_PUBLIC uint32_t outputColorIndex;
};

SHADER_PUBLIC struct DebugDrawPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(DebugLineSegment) segmentBuffer;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t totalLineSegments;
};

SHADER_PUBLIC struct GPUDebugDrawPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(GPUDebugDrawArgs) args;
    SHADER_PUBLIC SHADER_PTR(DebugLineSegment) segmentBuffer;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct GPUDebugBuildIndirectPushConstant
{
    SHADER_PUBLIC SHADER_PTR(GPUDebugDrawArgs) args;
    SHADER_PUBLIC SHADER_PTR(GPUDebugSphereArgs) sphereArgs;
    SHADER_PUBLIC SHADER_PTR(GPUDebugCubeArgs) cubeArgs;
};

SHADER_PUBLIC struct GPUDebugSphereDrawPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(DebugSphereInstance) instanceBuffer;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct ProbePreviewSpherePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t cubemapIndex;
    SHADER_PUBLIC float4 centerRadius;
    SHADER_PUBLIC float roughness;
    SHADER_PUBLIC uint32_t bIrradiance;
};

SHADER_PUBLIC struct GPUDebugCubeDrawPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(DebugCubeInstance) instanceBuffer;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct RadianceCacheDebugPushConstant
{
    SHADER_PUBLIC SHADER_PTR(GPUDebugCubeArgs) cubeArgs;
    SHADER_PUBLIC SHADER_PTR(DebugCubeInstance) cubeBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) entries;
    SHADER_PUBLIC SHADER_PTR(uint2) keys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) cells;
    SHADER_PUBLIC float exposure;
    SHADER_PUBLIC int32_t bucketFilter;
};

SHADER_PUBLIC struct DDGIProbeDebugPushConstant
{
    SHADER_PUBLIC DDGIVolumeParams volume;
    SHADER_PUBLIC SHADER_PTR(GPUDebugSphereArgs) sphereArgs;
    SHADER_PUBLIC SHADER_PTR(DebugSphereInstance) sphereBuffer;
    SHADER_PUBLIC SHADER_PTR(float4) probeOffsets;
    SHADER_PUBLIC uint32_t irradianceAtlasIndex;
    SHADER_PUBLIC uint32_t bOffsetsValid;
    SHADER_PUBLIC float probeDebugExposure;
    SHADER_PUBLIC uint32_t packedTint;
    SHADER_PUBLIC SHADER_PTR(uint32_t) probeActive;
    SHADER_PUBLIC uint32_t bActiveValid;
    SHADER_PUBLIC uint32_t bHideInactive;
    SHADER_PUBLIC uint32_t visibilityAtlasIndex;
    SHADER_PUBLIC uint32_t debugMode;
    SHADER_PUBLIC uint32_t warmupAge;
    SHADER_PUBLIC uint32_t pad9;
};

SHADER_PUBLIC struct GIDeconstructPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) ddgiCascades;
    SHADER_PUBLIC SHADER_PTR(uint) cacheEntries;
    SHADER_PUBLIC SHADER_PTR(uint2) cacheKeys;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheCell) cacheCells;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t mode;
    SHADER_PUBLIC uint32_t bCascadesValid;
};

SHADER_PUBLIC struct GIGatherDebugPushConstant
{
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t resolvedIndex;
    SHADER_PUBLIC uint32_t dataIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t mode;
    SHADER_PUBLIC uint32_t varGuideIndex;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct ClusterGridDebugPushConstant
{
    SHADER_PUBLIC SHADER_PTR(GPUDebugDrawArgs) args;
    SHADER_PUBLIC SHADER_PTR(DebugLineSegment) segmentBuffer;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC float zNear;
    SHADER_PUBLIC float zFar;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t pad0;
};

SHADER_PUBLIC struct DDGIProbeTracePushConstant
{
    SHADER_PUBLIC DDGIVolumeParams volume;
    SHADER_PUBLIC float4 rayRotation;
    SHADER_PUBLIC uint32_t previousBaseCellXY; // low 16 = x + 32768, high 16 = y + 32768
    SHADER_PUBLIC uint32_t previousBaseCellZFlags; // low 16 = z + 32768, bit 16 = feedback valid, bit 17 = bounce only
    SHADER_PUBLIC SHADER_PTR(float4) rayData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) indexBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC SHADER_PTR(float4) probeOffsets;
    SHADER_PUBLIC SHADER_PTR(DDGICascadeSetGPU) previousCascades;
    SHADER_PUBLIC SHADER_PTR(RadianceCacheBuffers) radianceCache;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(uint32_t) probeActive;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t raysAndShadeInterval; // low 16 = raysPerProbe, high 16 = radiance cache shade interval
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC float maxRayRadiance;
    SHADER_PUBLIC float bounceIntensity;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC uint32_t reflectionProbeCount;
    SHADER_PUBLIC SHADER_PTR(ReflectionProbeGPU) reflectionProbes;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridProbeGrid;
    SHADER_PUBLIC SHADER_PTR(uint2) worldGridBuffer;
    SHADER_PUBLIC SHADER_PTR(uint) worldGridIndexList;
};

SHADER_PUBLIC struct DDGIProbeBlendPushConstant
{
    SHADER_PUBLIC DDGIVolumeParams volume;
    SHADER_PUBLIC float4 rayRotation;
    SHADER_PUBLIC int3 previousBaseCell;
    SHADER_PUBLIC uint32_t bHistoryValid;
    SHADER_PUBLIC SHADER_PTR(float4) rayData;
    SHADER_PUBLIC SHADER_PTR(uint32_t) probeRestart;
    SHADER_PUBLIC uint32_t atlasOutIndex;
    SHADER_PUBLIC uint32_t pad0;
    SHADER_PUBLIC uint32_t raysPerProbe;
    SHADER_PUBLIC float hysteresis;
    SHADER_PUBLIC float distanceExponent;
    SHADER_PUBLIC float irradianceThreshold;
    SHADER_PUBLIC float brightnessThreshold;
    SHADER_PUBLIC uint32_t bRestartValid;
    SHADER_PUBLIC SHADER_PTR(uint32_t) probeActive;
    SHADER_PUBLIC uint32_t bActiveValid;
    SHADER_PUBLIC uint32_t pad1;
};

SHADER_PUBLIC struct DDGIProbeRelocatePushConstant
{
    SHADER_PUBLIC DDGIVolumeParams volume;
    SHADER_PUBLIC float4 rayRotation;
    SHADER_PUBLIC int3 previousBaseCell;
    SHADER_PUBLIC uint32_t bOffsetsValid;
    SHADER_PUBLIC SHADER_PTR(float4) rayData;
    SHADER_PUBLIC SHADER_PTR(float4) offsetsIn;
    SHADER_PUBLIC SHADER_PTR(float4) offsetsOut;
    SHADER_PUBLIC SHADER_PTR(uint32_t) restartOut;
    SHADER_PUBLIC uint32_t raysPerProbe;
    SHADER_PUBLIC float minFrontfaceDistance;
    SHADER_PUBLIC SHADER_PTR(uint32_t) activeIn;
    SHADER_PUBLIC SHADER_PTR(uint32_t) activeOut;
    SHADER_PUBLIC uint32_t bActiveValid;
    SHADER_PUBLIC uint32_t bClassify;
};

SHADER_PUBLIC struct EquirectToCubemapPushConstant
{
    SHADER_PUBLIC uint32_t samplerIndex;
    SHADER_PUBLIC uint32_t sourceEquiIndex;
    SHADER_PUBLIC uint32_t targetCubeIndex;
    SHADER_PUBLIC uint32_t cubemapWidth;
    SHADER_PUBLIC uint32_t cubemapHeight;
};

SHADER_PUBLIC struct ConvolveDiffusePushConstant
{
    SHADER_PUBLIC uint32_t samplerIndex;
    SHADER_PUBLIC uint32_t sourceIndex;
    SHADER_PUBLIC uint32_t targetIndex;
    SHADER_PUBLIC uint32_t targetWidth;
    SHADER_PUBLIC uint32_t targetHeight;
    SHADER_PUBLIC float sampleDelta;
};

SHADER_PUBLIC struct PrefilterSpecularPushConstant
{
    SHADER_PUBLIC uint32_t samplerIndex;
    SHADER_PUBLIC uint32_t sourceIndex;
    SHADER_PUBLIC uint32_t targetIndex;
    SHADER_PUBLIC float roughness;
    SHADER_PUBLIC uint32_t width;
    SHADER_PUBLIC uint32_t height;
    SHADER_PUBLIC uint32_t sampleCount;
    SHADER_PUBLIC float fireflyThreshold;
    SHADER_PUBLIC uint32_t sourceResolution;
};

SHADER_PUBLIC struct EnvironmentSkyboxPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC int32_t cubemapIndex;
    SHADER_PUBLIC int32_t skyboxLOD;
    SHADER_PUBLIC float intensity;
};

SHADER_PUBLIC struct BRDFLUTPushConstant
{
    SHADER_PUBLIC uint32_t targetIndex;
};

SHADER_PUBLIC struct ProceduralTextureBasePushConstant
{
    SHADER_PUBLIC uint32_t outputIndex;
};

SHADER_PUBLIC struct SMAAAreaGeneratePushConstant
{
    SHADER_PUBLIC uint32_t targetIndex;
};

SHADER_PUBLIC struct SMAASearchGeneratePushConstant
{
    SHADER_PUBLIC uint32_t targetIndex;
};

SHADER_PUBLIC struct TextRenderPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(WorldGlyphQuad) worldGlyphQuads;
    SHADER_PUBLIC SHADER_PTR(TextInstanceData) textInstanceData;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(TextRenderMaterial) textMaterialBuffer;
    SHADER_PUBLIC uint32_t quadOffset;
    SHADER_PUBLIC uint32_t quadCount;
    SHADER_PUBLIC uint32_t atlasBindlessIndex;
    SHADER_PUBLIC uint32_t textMaterialIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct UIRectRenderPushConstant
{
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float2 ndcMin;
    SHADER_PUBLIC float2 ndcMax;
    SHADER_PUBLIC float4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    SHADER_PUBLIC float2 pxMin;
    SHADER_PUBLIC float2 pxMax;
};

SHADER_PUBLIC struct UITextRenderPushConstant
{
    SHADER_PUBLIC SHADER_PTR(UIGlyphQuad) uiGlyphQuads;
    SHADER_PUBLIC uint32_t quadOffset;
    SHADER_PUBLIC uint32_t quadCount;
    SHADER_PUBLIC float4 colorTint;
    SHADER_PUBLIC uint32_t atlasBindlessIndex;
    SHADER_PUBLIC float pxRange;
};

SHADER_PUBLIC struct UIImagePushConstant
{
    SHADER_PUBLIC float2 ndcMin;
    SHADER_PUBLIC float2 ndcMax;
    SHADER_PUBLIC float2 uvMin;
    SHADER_PUBLIC float2 uvMax;
    SHADER_PUBLIC float4 tintColor;
    SHADER_PUBLIC float4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    SHADER_PUBLIC float2 pxMin;
    SHADER_PUBLIC float2 pxMax;
    SHADER_PUBLIC uint32_t imageBindlessIndex;
};

SHADER_PUBLIC struct UIBorderPushConstant
{
    SHADER_PUBLIC float2 ndcMin; // NDC position for vertex stage
    SHADER_PUBLIC float2 ndcMax;
    SHADER_PUBLIC float4 color;
    SHADER_PUBLIC float4 borderWidths; // x=left, y=right, z=top, w=bottom in pixels
    SHADER_PUBLIC float4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    SHADER_PUBLIC float2 pxMin; // pixel-space bounds for SDF in fragment stage
    SHADER_PUBLIC float2 pxMax;
};

SHADER_PUBLIC SHADER_CONST uint32_t SPRITE_FLAG_BILLBOARD = 1u;

SHADER_PUBLIC struct SpriteData
{
    SHADER_PUBLIC float3 worldPosition;
    SHADER_PUBLIC float pixelSize;
    SHADER_PUBLIC uint32_t packedColor; // RGBA8 unorm
    SHADER_PUBLIC uint32_t stableIdLo;
    SHADER_PUBLIC uint32_t stableIdHi;
    SHADER_PUBLIC uint32_t flags;
};

SHADER_PUBLIC struct SpritePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(SpriteData) sprites;
    SHADER_PUBLIC uint32_t spriteCount; // count for this batch
    SHADER_PUBLIC uint32_t spriteOffset; // start index into sprites for this batch
    SHADER_PUBLIC uint32_t textureIndex; // bindless texture index, uniform for this batch
    SHADER_PUBLIC uint32_t samplerIndex; // bindless sampler index, uniform for this batch
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

// =====================================================================
// NRD input/output adaptation push constants
// =====================================================================

SHADER_PUBLIC struct NrdPrepGuidesPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outNormalRoughnessIndex;
    SHADER_PUBLIC uint32_t outMvIndex;
    SHADER_PUBLIC uint32_t outViewZIndex;
    SHADER_PUBLIC uint2 rectSize;
};

SHADER_PUBLIC struct NrdRadianceCopyPushConstant
{
    SHADER_PUBLIC uint2 rectSize;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
};

SHADER_PUBLIC struct NrdReblurRadiancePackPushConstant
{
    SHADER_PUBLIC float4 hitDistParams; // xyz = A/B/C; NRD 4.17 has no D
    SHADER_PUBLIC uint2 rectSize;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
};

// =====================================================================
// RELAX DiffuseSpecular denoiser push constants
// This software contains source code provided by NVIDIA Corporation.
// Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
// https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
// =====================================================================

SHADER_PUBLIC struct RelaxGenerateViewZPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t outViewZIndex;
    SHADER_PUBLIC uint32_t outGuideIndex;
};

SHADER_PUBLIC struct RelaxClassifyTilesPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t tilesOutIndex;
};

SHADER_PUBLIC struct RelaxPrepassPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t specInputIndex;
    SHADER_PUBLIC uint32_t diffInputIndex;
    SHADER_PUBLIC uint32_t specOutIndex;
    SHADER_PUBLIC uint32_t diffOutIndex;
};

SHADER_PUBLIC struct RelaxTemporalAccumulationPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t prevNormalRoughnessIndex;
    SHADER_PUBLIC uint32_t prevViewZIndex;
    SHADER_PUBLIC uint32_t prevHistoryLengthIndex;
    SHADER_PUBLIC uint32_t specInputIndex;
    SHADER_PUBLIC uint32_t diffInputIndex;
    SHADER_PUBLIC uint32_t historySpecFastIndex;
    SHADER_PUBLIC uint32_t historyDiffFastIndex;
    SHADER_PUBLIC uint32_t historySpecIndex;
    SHADER_PUBLIC uint32_t historyDiffIndex;
    SHADER_PUBLIC uint32_t prevSpecHitDistIndex;
    SHADER_PUBLIC uint32_t outHistoryLengthIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecFastIndex;
    SHADER_PUBLIC uint32_t outDiffFastIndex;
    SHADER_PUBLIC uint32_t outSpecHitDistIndex;
    SHADER_PUBLIC uint32_t outSpecReprojConfidenceIndex;
    SHADER_PUBLIC uint32_t outPrevNRIndex;
    SHADER_PUBLIC uint32_t confidenceIndex;
};

SHADER_PUBLIC struct RelaxHistoryFixPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t historyLengthIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
};

SHADER_PUBLIC struct RelaxHistoryClampingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t historyLengthIndex;
    SHADER_PUBLIC uint32_t specFastIndex;
    SHADER_PUBLIC uint32_t diffFastIndex;
    SHADER_PUBLIC uint32_t specNoisyIndex;
    SHADER_PUBLIC uint32_t diffNoisyIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecFastIndex;
    SHADER_PUBLIC uint32_t outDiffFastIndex;
    SHADER_PUBLIC uint32_t outHistoryLengthIndex;
};

SHADER_PUBLIC struct RelaxAtrousPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t guideIndex;
    SHADER_PUBLIC uint32_t historyLengthIndex;
    SHADER_PUBLIC uint32_t specVarIndex;
    SHADER_PUBLIC uint32_t diffVarIndex;
    SHADER_PUBLIC uint32_t specReprojConfidenceIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t stepSize;
    SHADER_PUBLIC float chromaLumaPower;
};

SHADER_PUBLIC struct RelaxAntiFireflyPushConstant
{
    SHADER_PUBLIC SHADER_PTR(RelaxDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
};

// ===================== ReBLUR =====================

SHADER_PUBLIC struct ReblurPackPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t specInputIndex;
    SHADER_PUBLIC uint32_t diffInputIndex;
    SHADER_PUBLIC uint32_t specOutIndex;
    SHADER_PUBLIC uint32_t diffOutIndex;
};

SHADER_PUBLIC struct ReblurGenerateViewZPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t outViewZIndex;
};

SHADER_PUBLIC struct ReblurClassifyTilesPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t tilesOutIndex;
};

SHADER_PUBLIC struct ReblurPrepassPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t specInputIndex;
    SHADER_PUBLIC uint32_t diffInputIndex;
    SHADER_PUBLIC uint32_t specOutIndex;
    SHADER_PUBLIC uint32_t diffOutIndex;
};

SHADER_PUBLIC struct ReblurTemporalAccumulationPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t prevNormalRoughnessIndex;
    SHADER_PUBLIC uint32_t prevViewZIndex;
    SHADER_PUBLIC uint32_t prevInternalDataIndex;
    SHADER_PUBLIC uint32_t specInputIndex;
    SHADER_PUBLIC uint32_t diffInputIndex;
    SHADER_PUBLIC uint32_t historySpecFastIndex;
    SHADER_PUBLIC uint32_t historyDiffFastIndex;
    SHADER_PUBLIC uint32_t historySpecIndex;
    SHADER_PUBLIC uint32_t historyDiffIndex;
    SHADER_PUBLIC uint32_t prevSpecHitDistIndex;
    SHADER_PUBLIC uint32_t outData1Index;
    SHADER_PUBLIC uint32_t outData2Index;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecFastIndex;
    SHADER_PUBLIC uint32_t outDiffFastIndex;
    SHADER_PUBLIC uint32_t outSpecHitDistIndex;
    SHADER_PUBLIC uint32_t outPrevNRIndex;
    SHADER_PUBLIC uint32_t confidenceIndex;
};

SHADER_PUBLIC struct ReblurHistoryFixPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t data1Index;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t specFastIndex;
    SHADER_PUBLIC uint32_t diffFastIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t outSpecFastIndex;
    SHADER_PUBLIC uint32_t outDiffFastIndex;
};

SHADER_PUBLIC struct ReblurBlurPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t data1Index;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outSpecIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t isPostBlur;
};

SHADER_PUBLIC struct ReblurChromaPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t data1Index;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t outDiffIndex;
    SHADER_PUBLIC uint32_t stepSize;
    SHADER_PUBLIC float chromaLumaPower;
};

SHADER_PUBLIC struct ReblurStabilizationPushConstant
{
    SHADER_PUBLIC SHADER_PTR(ReblurDiffuseSpecularConstants) constants;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t normalRoughnessIndex;
    SHADER_PUBLIC uint32_t viewZIndex;
    SHADER_PUBLIC uint32_t specIndex;
    SHADER_PUBLIC uint32_t diffIndex;
    SHADER_PUBLIC uint32_t data1Index;
    SHADER_PUBLIC uint32_t data2Index;
    SHADER_PUBLIC uint32_t specHitDistIndex;
    SHADER_PUBLIC uint32_t prevSpecLumaStabIndex;
    SHADER_PUBLIC uint32_t prevDiffLumaStabIndex;
    SHADER_PUBLIC uint32_t outSpecLumaStabIndex;
    SHADER_PUBLIC uint32_t outDiffLumaStabIndex;
    SHADER_PUBLIC uint32_t outSpecFinalIndex;
    SHADER_PUBLIC uint32_t outDiffFinalIndex;
    SHADER_PUBLIC uint32_t outInternalDataIndex;
};

SHADER_PUBLIC struct DepthCopyPushConstant
{
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint2 extents;
};

SHADER_PUBLIC struct ColorCopyPushConstant
{
    SHADER_PUBLIC uint32_t srcIndex;
    SHADER_PUBLIC uint32_t dstIndex;
    SHADER_PUBLIC uint2 extents;
};

SHADER_PUBLIC struct RTShadowTestPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint2 renderExtent;
};

SHADER_PUBLIC struct RTGroundTruthDIPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(float4) accumulationBuffer;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t accumulationCount;
    SHADER_PUBLIC uint32_t pad0;
    SHADER_PUBLIC uint2 renderExtent;
};

SHADER_PUBLIC struct RTGroundTruthGIPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(float4) accumulationBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) indexBuffer;
    SHADER_PUBLIC SHADER_PTR(VertexAttribute) vertexAttrBuffer;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t accumulationCount;
    SHADER_PUBLIC float iblIntensity;
    SHADER_PUBLIC uint2 renderExtent;
};

SHADER_PUBLIC struct RTSunShadowPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t tlasIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint2 fullExtent;
    SHADER_PUBLIC uint32_t pixelScale;
    SHADER_PUBLIC uint32_t outputDepthIndex; // for half res
    SHADER_PUBLIC uint32_t outputGbufferIndex; // for half res
};

SHADER_PUBLIC struct DirectionalLightPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t shadowIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint2 shadowExtent;
    SHADER_PUBLIC uint32_t pixelScale;
    SHADER_PUBLIC uint32_t shadowDepthIndex; // for half res
    SHADER_PUBLIC uint32_t shadowNormalIndex; // for half res
};

SHADER_PUBLIC struct SigmaClassifyPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t shadowIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC float maxKernelPixels;
};

SHADER_PUBLIC struct SigmaSmoothTilesPushConstant
{
    SHADER_PUBLIC uint2 tilesExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
};

SHADER_PUBLIC struct SigmaBlurPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t shadowIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t frameIndex;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC uint32_t passIndex; // 0 = blur (derive shadow from penumbra), 1 = post-blur (unpack sqrt)
    SHADER_PUBLIC float maxKernelPixels;
    SHADER_PUBLIC float penumbraScale;
};

SHADER_PUBLIC struct SigmaTemporalPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint2 renderExtent;
    SHADER_PUBLIC uint32_t shadowIndex;
    SHADER_PUBLIC uint32_t historyIndex; // ~0u when no history this frame
    SHADER_PUBLIC uint32_t historyLengthIndex; // ~0u when no history this frame
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t outHistoryLengthIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t tilesIndex;
    SHADER_PUBLIC float stabilizationStrength;
    SHADER_PUBLIC uint32_t pixelScale;
};

#endif //WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
