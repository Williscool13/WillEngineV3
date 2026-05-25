//
// Created by William on 2025-12-28.
//

#ifndef WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
#define WILL_ENGINE_PUSH_CONSTANT_INTEROP_H

#ifdef __SLANG__
module push_constant_interop;
#define SHADER_PUBLIC public
#define SHADER_CONST const static
#define SHADER_ALIGN
#define SHADER_PTR(T) T*
#define SHADER_ATOMIC(T) Atomic<T>

import common_interop;
import model_interop;
import constants_interop;
import instancing_interop;
import shadows_interop;
import lights_interop;
import text_interop;
import ui_interop;
#else
#include <glm/glm.hpp>
#include <volk.h>
#include <cstdint>
#include "common_interop.h"
#include "model_interop.h"
#include "instancing_interop.h"
#include "text_interop.h"
#include "ui_interop.h"

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
#define SHADER_ALIGN alignas(16)
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
    SHADER_PUBLIC int2 srcExtent;
    SHADER_PUBLIC int2 dstExtent;
    SHADER_PUBLIC float nearPlane;
    SHADER_PUBLIC float farPlane;
    SHADER_PUBLIC uint textureArrayIndex;
    SHADER_PUBLIC uint textureIndexInArray;
    SHADER_PUBLIC uint valueTransformationType;
    SHADER_PUBLIC uint outputImageIndex;
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

SHADER_PUBLIC struct InstanceLODShadowsPushConstant
{
    // Read-Only
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;

    // Write
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;

    // Read-Only
    SHADER_PUBLIC uint32_t instanceCount;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t cascadeIndex;
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

SHADER_PUBLIC struct ExpandMeshletsShadowsPushConstant
{
    SHADER_PUBLIC SHADER_PTR(InstancingMeshletDispatchIndirect) indirectDispatchBuffer; // for totalMeshletCount
    SHADER_PUBLIC SHADER_PTR(InstanceMeshletOffsetPrefixSum) instanceMeshletOffsets;
    SHADER_PUBLIC SHADER_PTR(IntermediateMeshlet) intermediateMeshlets;

    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;

    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t cascadeIndex;
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

SHADER_PUBLIC struct BaseMeshShadingPushConstant
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
    SHADER_PUBLIC SHADER_PTR(CompactedMeshlet) visibleMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingCompactedMeshletDispatchIndirect) compactedDispatchBuffer; // for "total visible meshlets"
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t customData[39]; // vk1.4 target, custom PC space that can be filled by anything needed
};

SHADER_PUBLIC struct ShadowMeshShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(VertexPosition) positionBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletVerticesBuffer;
    SHADER_PUBLIC SHADER_PTR(uint32_t) meshletTrianglesBuffer;
    SHADER_PUBLIC SHADER_PTR(Meshlet) meshletBuffer;
    SHADER_PUBLIC SHADER_PTR(Primitive) primitiveBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC SHADER_PTR(MaterialProperties) materialBuffer;
    SHADER_PUBLIC SHADER_PTR(Model) modelBuffer;
    SHADER_PUBLIC SHADER_PTR(CompactedMeshlet) visibleMeshlets;
    SHADER_PUBLIC SHADER_PTR(InstancingCompactedMeshletDispatchIndirect) compactedDispatchBuffer; // for "total visible meshlets"
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t cascadeIndex;
    SHADER_PUBLIC uint32_t customData[38]; // vk1.4 target, custom PC space that can be filled by anything needed
};

SHADER_PUBLIC struct ShadowsResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC int32_t gtaoFilteredIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
    SHADER_PUBLIC int4 csmIndices;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
};

SHADER_PUBLIC struct SHADER_ALIGN LightingResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC SHADER_PTR(LightingDispatchParameters) lightDispatchBuffer;
    SHADER_PUBLIC SHADER_PTR(Instance) instanceBuffer;
    SHADER_PUBLIC uint32_t visibilityBufferIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t shadowsIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC uint32_t lightingIndex;
};

SHADER_PUBLIC struct SHADER_ALIGN DeferredResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferTwoIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t shadowsIndex;
    SHADER_PUBLIC int32_t skyboxIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct TemporalAntialiasingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t colorResolvedIndex;
    SHADER_PUBLIC uint32_t depthIndex;
    SHADER_PUBLIC uint32_t colorHistoryIndex;
    SHADER_PUBLIC uint32_t gbufferOneIndex;
    SHADER_PUBLIC uint32_t gbufferOneHistoryIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
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

SHADER_PUBLIC struct TonemapSDRPushConstant
{
    // -1=None, 0=ACES Fitted, 1=Hable, 2=Reinhard, 3=Lottes, 4=Reinhard-Jodie, 5=Clamp, 6=Hejl-Burgess-Dawson, 7=Uchimura, 8=ACES Narkowicz, 9=AgX, 10=Khronos PBR Neutral
    SHADER_PUBLIC int32_t tonemapOperator;
    SHADER_PUBLIC float targetLuminance;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC uint32_t bloomImageIndex;
    SHADER_PUBLIC float bloomIntensity;
    SHADER_PUBLIC uint32_t outputWidth;
    SHADER_PUBLIC uint32_t outputHeight;
    SHADER_PUBLIC uint32_t srcImageIndex;
    SHADER_PUBLIC uint32_t dstImageIndex;
    // Hable:   [0]=whitePoint
    // Reinhard:[0]=whitePoint
    // Uchimura:[0]=P, [1]=a, [2]=m, [3]=l, [4]=c, [5]=b
    // AgX:     [0]=minEV, [1]=maxEV
    // Khronos: [0]=startCompression, [1]=desaturation
    SHADER_PUBLIC float params[6];
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
    SHADER_PUBLIC SHADER_PTR(uint32_t) histogramBufferAddress;
    SHADER_PUBLIC uint32_t width;
    SHADER_PUBLIC uint32_t height;
    SHADER_PUBLIC float minLogLuminance;
    SHADER_PUBLIC float oneOverLogLuminanceRange;
};

SHADER_PUBLIC struct ExposureCalculatePushConstant
{
    SHADER_PUBLIC SHADER_PTR(uint32_t) histogramBufferAddress;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC float minLogLuminance;
    SHADER_PUBLIC float logLuminanceRange;
    SHADER_PUBLIC float adaptationSpeed;
    SHADER_PUBLIC uint32_t totalPixels;
};

SHADER_PUBLIC struct MotionBlurTileVelocityPushConstant
{
    SHADER_PUBLIC uint2 velocityBufferSize;
    SHADER_PUBLIC uint2 tileBufferSize;
    SHADER_PUBLIC uint32_t velocityBufferIndex;
    SHADER_PUBLIC uint32_t tileMaxIndex;
};

SHADER_PUBLIC struct MotionBlurNeighborMaxPushConstant
{
    SHADER_PUBLIC uint2 tileBufferSize;
    SHADER_PUBLIC uint tileMaxIndex;
    SHADER_PUBLIC uint neighborMaxIndex;
};

SHADER_PUBLIC struct MotionBlurReconstructionPushConstant
{
    SHADER_PUBLIC uint2 srcBufferSize;
    SHADER_PUBLIC uint32_t sceneColorIndex;
    SHADER_PUBLIC uint32_t velocityBufferIndex;
    SHADER_PUBLIC uint32_t depthBufferIndex;
    SHADER_PUBLIC uint32_t tileNeighborMaxIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float velocityScale; // 1.0f default
    SHADER_PUBLIC float depthScale; // 1.0f?
};

SHADER_PUBLIC struct BloomThresholdPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputColorIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float threshold;
    SHADER_PUBLIC float softThreshold;
};

SHADER_PUBLIC struct BloomDownsamplePushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t srcMipLevel;
};

SHADER_PUBLIC struct BloomUpsamplePushConstant
{
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC uint32_t lowerMipLevel;
    SHADER_PUBLIC uint32_t higherMipLevel;
    SHADER_PUBLIC float radius;
};

SHADER_PUBLIC struct VignetteChromaticAberrationPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float chromaticAberrationStrength;
    SHADER_PUBLIC float vignetteStrength;
    SHADER_PUBLIC float vignetteRadius;
    SHADER_PUBLIC float vignetteSmoothness;
};

SHADER_PUBLIC struct FilmGrainPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float grainStrength;
    SHADER_PUBLIC float grainSize;
    SHADER_PUBLIC uint frameIndex;
};

SHADER_PUBLIC struct DitherPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float ditherStrength;
    SHADER_PUBLIC float frameIndex;
};

SHADER_PUBLIC struct SharpeningPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float sharpness;
};

SHADER_PUBLIC struct ColorGradingPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float exposure;
    SHADER_PUBLIC float contrast;
    SHADER_PUBLIC float saturation;
    SHADER_PUBLIC float temperature;
    SHADER_PUBLIC float tint;
};

SHADER_PUBLIC struct PaniniProjectionPushConstant
{
    SHADER_PUBLIC uint2 outputExtent;
    SHADER_PUBLIC uint32_t inputIndex;
    SHADER_PUBLIC uint32_t outputIndex;
    SHADER_PUBLIC float perspectiveFov;
    SHADER_PUBLIC float aspect;
    SHADER_PUBLIC float strength;
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
};

SHADER_PUBLIC struct EnvironmentSkyboxPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC int32_t cubemapIndex;
    SHADER_PUBLIC int32_t skyboxLOD;
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

#endif //WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
