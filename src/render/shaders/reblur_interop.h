/*
 * ReBLUR denoiser constants struct.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

#ifndef WILL_ENGINE_REBLUR_INTEROP_H
#define WILL_ENGINE_REBLUR_INTEROP_H

#ifdef __SLANG__
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

/** @brief Per-frame constants consumed by all ReBLUR DiffuseSpecular passes. Pass as SHADER_PTR.
 *  Geometry/reconstruction fields match RelaxDiffuseSpecularConstants so relax_utils.slang helpers are reused. */
SHADER_PUBLIC struct ReblurDiffuseSpecularConstants
{
    // Matrices (320 bytes)
    SHADER_PUBLIC float4x4 gWorldToClip;
    SHADER_PUBLIC float4x4 gWorldToClipPrev;
    SHADER_PUBLIC float4x4 gWorldToViewPrev;
    SHADER_PUBLIC float4x4 gWorldPrevToWorld;
    SHADER_PUBLIC float4x4 gViewToWorld;

    // Frustum vectors (144 bytes)
    SHADER_PUBLIC float4 gRotatorPre;
    SHADER_PUBLIC float4 gFrustumRight;
    SHADER_PUBLIC float4 gFrustumUp;
    SHADER_PUBLIC float4 gFrustumForward;
    SHADER_PUBLIC float4 gPrevFrustumRight;
    SHADER_PUBLIC float4 gPrevFrustumUp;
    SHADER_PUBLIC float4 gPrevFrustumForward;
    SHADER_PUBLIC float4 gCameraDelta;
    SHADER_PUBLIC float4 gMvScale;

    // ReBLUR float4 settings (32 bytes)
    SHADER_PUBLIC float4 gHitDistParams;        // hit distance normalization: x=A, y=B, z=C, w=D
    SHADER_PUBLIC float4 gConvergenceSettings;  // x=s, y=b, z=p, w=unused

    // Float2 parameters (80 bytes, all 8-byte aligned, even count)
    SHADER_PUBLIC float2 gJitter;
    SHADER_PUBLIC float2 gResolutionScale;
    SHADER_PUBLIC float2 gRectOffset;
    SHADER_PUBLIC float2 gResourceSizeInv;
    SHADER_PUBLIC float2 gResourceSize;
    SHADER_PUBLIC float2 gRectSizeInv;
    SHADER_PUBLIC float2 gRectSizePrev;
    SHADER_PUBLIC float2 gResourceSizeInvPrev;
    SHADER_PUBLIC float2 gAntilagSettings;                              // x: luminanceSigmaScale, y: luminanceSensitivity
    SHADER_PUBLIC float2 gSpecProbabilityThresholdsForMvModification;   // x: low, y: high

    // Integer parameters (32 bytes)
    SHADER_PUBLIC uint2 gPrintfAt;
    SHADER_PUBLIC uint2 gRectOrigin;
    SHADER_PUBLIC int2 gRectSize;
    SHADER_PUBLIC int2 _pad0;

    // Float parameters (104 bytes, even count)
    SHADER_PUBLIC float gMaxAccumulatedFrameNum;
    SHADER_PUBLIC float gMaxFastAccumulatedFrameNum;
    SHADER_PUBLIC float gMaxStabilizedFrameNum;
    SHADER_PUBLIC float gDisocclusionThreshold;
    SHADER_PUBLIC float gDisocclusionThresholdAlternate;
    SHADER_PUBLIC float gDenoisingRange;
    SHADER_PUBLIC float gPlaneDistSensitivity;
    SHADER_PUBLIC float gFramerateScale;
    SHADER_PUBLIC float gMinBlurRadius;
    SHADER_PUBLIC float gMaxBlurRadius;
    SHADER_PUBLIC float gDiffPrepassBlurRadius;
    SHADER_PUBLIC float gSpecPrepassBlurRadius;
    SHADER_PUBLIC float gLobeAngleFraction;
    SHADER_PUBLIC float gRoughnessFraction;
    SHADER_PUBLIC float gHistoryFixFrameNum;
    SHADER_PUBLIC float gHistoryFixBasePixelStride;
    SHADER_PUBLIC float gFastHistoryClampingSigmaScale;
    SHADER_PUBLIC float gStabilizationStrength;
    SHADER_PUBLIC float gFireflySuppressorMinRelativeScale;
    SHADER_PUBLIC float gMinHitDistanceWeight;
    SHADER_PUBLIC float gResponsiveAccumulationInvRoughnessThreshold;
    SHADER_PUBLIC float gCheckerboardResolveAccumSpeed;
    SHADER_PUBLIC float gUnproject;
    SHADER_PUBLIC float gOrthoMode;
    SHADER_PUBLIC float depthLinearizeMult;
    SHADER_PUBLIC float depthLinearizeAdd;
    SHADER_PUBLIC float gDebug;
    SHADER_PUBLIC float _padF0;

    // Uint parameters (32 bytes, even count)
    SHADER_PUBLIC uint gFrameIndex;
    SHADER_PUBLIC uint gResetHistory;
    SHADER_PUBLIC uint gAntiFirefly;
    SHADER_PUBLIC uint gHitDistanceReconstructionMode;
    SHADER_PUBLIC uint gHasHistoryConfidence;
    SHADER_PUBLIC uint gHasDisocclusionThresholdMix;
    SHADER_PUBLIC uint gResponsiveAccumulationMinAccumulatedFrameNum;
    SHADER_PUBLIC uint gReturnHistoryLengthInsteadOfOcclusion;
};

#endif // WILL_ENGINE_REBLUR_INTEROP_H
