/*
 * RELAX denoiser constants struct.
 * This software contains source code provided by NVIDIA Corporation.
 */

#ifndef WILL_ENGINE_RELAX_INTEROP_H
#define WILL_ENGINE_RELAX_INTEROP_H

#ifdef __SLANG__
module relax_interop;
#define SHADER_PUBLIC public
#else
#include <glm/glm.hpp>
#include <cstdint>

using uint = uint32_t;
using float2 = glm::vec2;
using float3 = glm::vec3;
using float4 = glm::vec4;
using int2 = glm::ivec2;
using uint2 = glm::uvec2;
using float4x4 = glm::mat4;

#define SHADER_PUBLIC
#endif

/** @brief Per-frame constants consumed by all RELAX DiffuseSpecular passes. Pass as SHADER_PTR. */
SHADER_PUBLIC struct RelaxDiffuseSpecularConstants
{
    // Matrices (320 bytes)
    SHADER_PUBLIC float4x4 gWorldToClip;
    SHADER_PUBLIC float4x4 gWorldToClipPrev;
    SHADER_PUBLIC float4x4 gWorldToViewPrev;
    SHADER_PUBLIC float4x4 gWorldPrevToWorld;
    SHADER_PUBLIC float4x4 gViewToWorld;

    // Frustum vectors (208 bytes)
    SHADER_PUBLIC float4 gRotatorPre;
    SHADER_PUBLIC float4 gFrustumRight;
    SHADER_PUBLIC float4 gFrustumUp;
    SHADER_PUBLIC float4 gFrustumForward;
    SHADER_PUBLIC float4 gPrevFrustumRight;
    SHADER_PUBLIC float4 gPrevFrustumUp;
    SHADER_PUBLIC float4 gPrevFrustumForward;
    SHADER_PUBLIC float4 gCameraDelta;
    SHADER_PUBLIC float4 gMvScale;

    // Float2 parameters (64 bytes, all 8-byte aligned)
    SHADER_PUBLIC float2 gJitter;
    SHADER_PUBLIC float2 gResolutionScale;
    SHADER_PUBLIC float2 gRectOffset;
    SHADER_PUBLIC float2 gResourceSizeInv;
    SHADER_PUBLIC float2 gResourceSize;
    SHADER_PUBLIC float2 gRectSizeInv;
    SHADER_PUBLIC float2 gRectSizePrev;
    SHADER_PUBLIC float2 gResourceSizeInvPrev;

    // Integer parameters (32 bytes)
    SHADER_PUBLIC uint2 gPrintfAt;
    SHADER_PUBLIC uint2 gRectOrigin;
    SHADER_PUBLIC int2 gRectSize;
    SHADER_PUBLIC int2 _pad0;

    // Float parameters
    SHADER_PUBLIC float gSpecMaxAccumulatedFrameNum;
    SHADER_PUBLIC float gSpecMaxFastAccumulatedFrameNum;
    SHADER_PUBLIC float gDiffMaxAccumulatedFrameNum;
    SHADER_PUBLIC float gDiffMaxFastAccumulatedFrameNum;
    SHADER_PUBLIC float gDisocclusionThreshold;
    SHADER_PUBLIC float gDisocclusionThresholdAlternate;
    SHADER_PUBLIC float gCameraAttachedReflectionMaterialID;
    SHADER_PUBLIC float gStrandMaterialID;
    SHADER_PUBLIC float gStrandThickness;
    SHADER_PUBLIC float gRoughnessFraction;
    SHADER_PUBLIC float gSpecVarianceBoost;
    SHADER_PUBLIC float gSplitScreen;
    SHADER_PUBLIC float gDiffBlurRadius;
    SHADER_PUBLIC float gSpecBlurRadius;
    SHADER_PUBLIC float gDepthThreshold;
    SHADER_PUBLIC float gLobeAngleFraction;
    SHADER_PUBLIC float gSpecLobeAngleSlack;
    SHADER_PUBLIC float gHistoryFixEdgeStoppingNormalPower;
    SHADER_PUBLIC float gRoughnessEdgeStoppingRelaxation;
    SHADER_PUBLIC float gNormalEdgeStoppingRelaxation;
    SHADER_PUBLIC float gFastHistoryClampingSigmaScale;
    SHADER_PUBLIC float gHistoryAccelerationAmount;
    SHADER_PUBLIC float gHistoryResetTemporalSigmaScale;
    SHADER_PUBLIC float gHistoryResetSpatialSigmaScale;
    SHADER_PUBLIC float gHistoryResetAmount;
    SHADER_PUBLIC float gDenoisingRange;
    SHADER_PUBLIC float gSpecPhiLuminance;
    SHADER_PUBLIC float gDiffPhiLuminance;
    SHADER_PUBLIC float gDiffMaxLuminanceRelativeDifference;
    SHADER_PUBLIC float gSpecMaxLuminanceRelativeDifference;
    SHADER_PUBLIC float gLuminanceEdgeStoppingRelaxation;
    SHADER_PUBLIC float gConfidenceDrivenRelaxationMultiplier;
    SHADER_PUBLIC float gConfidenceDrivenLuminanceEdgeStoppingRelaxation;
    SHADER_PUBLIC float gConfidenceDrivenNormalEdgeStoppingRelaxation;
    SHADER_PUBLIC float gDebug;
    SHADER_PUBLIC float gOrthoMode;
    SHADER_PUBLIC float gUnproject;
    SHADER_PUBLIC float gFramerateScale;
    SHADER_PUBLIC float gCheckerboardResolveAccumSpeed;
    SHADER_PUBLIC float gHistoryFixFrameNum;
    SHADER_PUBLIC float gHistoryFixBasePixelStride;
    SHADER_PUBLIC float gHistoryFixAlternatePixelStride;
    SHADER_PUBLIC float gHistoryFixAlternatePixelStrideMaterialID;
    SHADER_PUBLIC float gHistoryThreshold;
    SHADER_PUBLIC float depthLinearizeMult;
    SHADER_PUBLIC float depthLinearizeAdd;
    SHADER_PUBLIC float gMinHitDistanceWeight;
    SHADER_PUBLIC float gDiffMinMaterial;
    SHADER_PUBLIC float gSpecMinMaterial;

    // Uint parameters
    SHADER_PUBLIC uint gRoughnessEdgeStoppingEnabled;
    SHADER_PUBLIC uint gFrameIndex;
    SHADER_PUBLIC uint gDiffCheckerboard;
    SHADER_PUBLIC uint gSpecCheckerboard;
    SHADER_PUBLIC uint gHasHistoryConfidence;
    SHADER_PUBLIC uint gHasDisocclusionThresholdMix;
    SHADER_PUBLIC uint gResetHistory;
    SHADER_PUBLIC uint _pad1;
};

#endif // WILL_ENGINE_RELAX_INTEROP_H
