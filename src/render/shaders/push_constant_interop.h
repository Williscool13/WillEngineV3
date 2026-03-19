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
#else
#include <glm/glm.hpp>
#include <volk.h>
#include <cstdint>
#include "common_interop.h"
#include "model_interop.h"
#include "instancing_interop.h"

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

SHADER_PUBLIC struct BaseMeshShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(Vertex) vertexBuffer;
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
    SHADER_PUBLIC uint32_t customData[41]; // vk1.4 target, custom PC space that can be filled by anything needed
};

SHADER_PUBLIC struct ShadowMeshShadingPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(Vertex) vertexBuffer;
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
    SHADER_PUBLIC uint32_t normalIndex;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct SHADER_ALIGN DeferredResolvePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC SHADER_PTR(ShadowData) shadowData;
    SHADER_PUBLIC SHADER_PTR(LightData) lightData;
    SHADER_PUBLIC uint32_t albedoIndex;
    SHADER_PUBLIC uint32_t normalIndex;
    SHADER_PUBLIC uint32_t pbrIndex;
    SHADER_PUBLIC uint32_t emissiveIndex;
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
    SHADER_PUBLIC uint32_t velocityIndex;
    SHADER_PUBLIC uint32_t velocityHistoryIndex;
    SHADER_PUBLIC uint32_t outputImageIndex;
};

SHADER_PUBLIC struct TonemapSDRPushConstant
{
    // 0=ACES, 1=Uncharted2, 2=Reinhard, 3=Lottes
    SHADER_PUBLIC int32_t tonemapOperator;
    SHADER_PUBLIC float targetLuminance;
    SHADER_PUBLIC SHADER_PTR(float) luminanceBufferAddress;
    SHADER_PUBLIC uint32_t bloomImageIndex;
    SHADER_PUBLIC float bloomIntensity;
    SHADER_PUBLIC uint32_t outputWidth;
    SHADER_PUBLIC uint32_t outputHeight;
    SHADER_PUBLIC uint32_t srcImageIndex;
    SHADER_PUBLIC uint32_t dstImageIndex;
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

    SHADER_PUBLIC SHADER_PTR(Vertex) vertexBuffer;
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
    SHADER_PUBLIC float strength;
    SHADER_PUBLIC float compression;
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
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct GTAOMainPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t prefilteredDepthIndex;
    SHADER_PUBLIC uint32_t normalBufferIndex;
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
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct GTAODenoisePushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t rawAOIndex;
    SHADER_PUBLIC uint32_t edgeDataIndex;
    SHADER_PUBLIC uint32_t filteredAOIndex;
    SHADER_PUBLIC float denoiseBlurBeta;
    SHADER_PUBLIC uint32_t isFinalDenoisePass;
    SHADER_PUBLIC uint32_t sceneDataIndex;
};

SHADER_PUBLIC struct PortalCompositePushConstant
{
    SHADER_PUBLIC uint32_t portalColorIndex;
    SHADER_PUBLIC uint32_t portalVelocityIndex;
    SHADER_PUBLIC uint32_t portalDepthIndex;
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
};

SHADER_PUBLIC struct EnvironmentSkyboxPushConstant
{
    SHADER_PUBLIC SHADER_PTR(SceneData) sceneData;
    SHADER_PUBLIC uint32_t sceneDataIndex;
    SHADER_PUBLIC int32_t cubemapIndex;
};

SHADER_PUBLIC struct BRDFLUTPushConstant
{
    SHADER_PUBLIC uint32_t targetIndex;
};

#endif //WILL_ENGINE_PUSH_CONSTANT_INTEROP_H
