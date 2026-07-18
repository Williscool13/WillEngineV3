/*
 * ReBLUR: Blur / Post-Blur. Faithful port of NRD REBLUR_Blur / REBLUR_PostBlur (DIFFUSE_SPECULAR, YCoCg=1).
 * Poisson-8 spatial filter: diffuse uses a screen-space isotropic disk, specular an anisotropic world-space
 * kernel stretched along the reflection (ReblurGetKernelBasis). Radius is driven by hit distance and the
 * non-linear accumulation speed; taps are edge-stopped by geometry/plane, normal, roughness (spec) and an
 * exponential hit-distance weight, plus the Poisson Gaussian. One pipeline serves both passes: pc.isPostBlur
 * widens the radius (REBLUR_POST_BLUR_RADIUS_SCALE = 2) and tightens the weight fractions (fractionScale 0.5).
 * Signal is YCoCg radiance in .xyz with a normalized hit distance in .w. Sky / out-of-range pass through.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import denoiser_common;
import reblur_common;

// REBLUR_Config.hlsli (inlined). Post-blur widens the radius (x2) and tightens edge-stopping fractions
// (fractionScale narrows the normal / roughness tolerances vs the blur pass).
static const float REBLUR_POST_BLUR_RADIUS_SCALE = 2.0;
static const float REBLUR_BLUR_FRACTION_SCALE = 1.0;
static const float REBLUR_POST_BLUR_FRACTION_SCALE = 0.5;

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurBlurMain(uniform ReblurBlurPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;
    float3x3 viewToWorld = (float3x3)rc.gViewToWorld;

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    float4 diffCenter = rdgTexturesFloat4[pc.diffIndex].Load(int3(pixelPos, 0));
    float4 specCenter = rdgTexturesFloat4[pc.specIndex].Load(int3(pixelPos, 0));
    if (isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y) {
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = specCenter;
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diffCenter;
        return;
    }

    float centerViewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
    if (!RelaxIsInDenoisingRange(centerViewZ, rc.gDenoisingRange)) {
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = specCenter;
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = diffCenter;
        return;
    }

    float4 centerNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0)), viewToWorld);
    float3 centerNormal = centerNR.xyz;
    float centerRoughness = centerNR.w;
    float3 centerWorldPos = RelaxGetCurrentWorldPosFromPixelPos(pixelPos, centerViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

    // World-space view vector (surface -> camera), needed for the specular dominant direction.
    float3 cameraPos = MLAffineTransform(rc.gViewToWorld, float3(0.0, 0.0, 0.0));
    float3 V = rc.gOrthoMode == 0.0 ? normalize(cameraPos - centerWorldPos) : -normalize(rc.gFrustumForward.xyz);

    // Per-lobe accum frames from DATA1; radius decays slower than 1/(1+N) so blur outlasts noise.
    float2 frameNums = ReblurUnpackData1(rdgTexturesFloat2[pc.data1Index].Load(int3(pixelPos, 0)));
    float diffNonLinearAccumSpeed = ReblurGetAdvancedNonLinearAccumSpeed(frameNums.x, rc.gMaxAccumulatedFrameNum, rc.gConvergenceSettings.xyz);
    float specNonLinearAccumSpeed = ReblurGetAdvancedNonLinearAccumSpeed(frameNums.y, rc.gMaxAccumulatedFrameNum, rc.gConvergenceSettings.xyz);

    float frustumSize = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, min(float(rc.gRectSize.x), float(rc.gRectSize.y)), centerViewZ);

    float kernelAngle = MLBayer4x4(uint2(pixelPos), rc.gFrameIndex) * MLPi(2.0);
    float4 rotator = MLCombineRotators(rc.gRotatorPre, MLGetRotator(kernelAngle));

    bool isPostBlur = pc.isPostBlur != 0u;
    float radiusScale = isPostBlur ? REBLUR_POST_BLUR_RADIUS_SCALE : 1.0;
    float fractionScale = isPostBlur ? REBLUR_POST_BLUR_FRACTION_SCALE : REBLUR_BLUR_FRACTION_SCALE;

    // ---------------- Diffuse blur (screen-space isotropic disk) ----------------
    // NRD: areaFactor = hitDistFactor * nonLinearAccumSpeed, hitDistFactor from de-normalized world hit distance
    // (roughness = 1 for diffuse), blurRadius = gMaxBlurRadius * sqrt01(areaFactor) * radiusScale.
    {
        float worldHitDist = ReblurDenormHitDist(diffCenter.w, centerViewZ, rc.gHitDistParams, 1.0);
        float hitDistFactor = RelaxGetHitDistFactor(worldHitDist, frustumSize);
        float areaFactor = hitDistFactor * diffNonLinearAccumSpeed;
        float blurRadius = ReblurGetBlurRadius(rc.gMaxBlurRadius, rc.gMinBlurRadius, areaFactor, radiusScale);

        float2 geometryWeightParams = RelaxGetGeometryWeightParams(rc.gPlaneDistSensitivity, frustumSize, centerWorldPos, centerNormal);
        float normalWeightParam = ReblurGetNormalWeightParam(diffNonLinearAccumSpeed, rc.gLobeAngleFraction, 1.0) / fractionScale;
        float2 hitDistanceWeightParams = ReblurGetHitDistanceWeightParams(diffCenter.w, diffNonLinearAccumSpeed, 1.0);
        // Hit-dist floor decays with convergence to preserve contact details; applied additively.
        float minHitDistWeight = rc.gMinHitDistanceWeight * fractionScale * diffNonLinearAccumSpeed;

        // Shrink the disk along the projected normal at grazing angles.
        float3 Nv = mul(transpose(viewToWorld), centerNormal);
        float NoV = abs(dot(centerNormal, V));
        float2 skew = lerp(1.0 - abs(Nv.xy), float2(1.0, 1.0), NoV);
        skew /= max(skew.x, skew.y);

        float4 sum = diffCenter;
        float weightSum = 1.0;

        [unroll]
        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 rotated = MLRotateVector(rotator, offset.xy) * skew;
            float2 samplePosF = floor(float2(pixelPos) + 0.5 + rotated * blurRadius) + 0.5;
            float2 uv = samplePosF * rc.gRectSizeInv;
            float w = RelaxGetGaussianWeight(offset.z);
            if (RelaxIsInScreenNearest(uv) == 0.0) {
                // Mirror instead of dropping the tap; Gaussian weight is invalid after mirroring.
                uv = RelaxMirrorUv(uv);
                w = 1.0;
            }

            int2 samplePos = int2(uv * float2(rc.gRectSize));
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, samplePos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float3 sampleNormal = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(samplePos, 0)), viewToWorld).xyz;
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(samplePos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float NoX = dot(centerNormal, sampleWorldPos);
            w = RelaxApplyGeometryWeightAndRangeCheck(w, sampleViewZ, NoX, geometryWeightParams, rc.gDenoisingRange);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNormal));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);

            float4 s = rdgTexturesFloat4[pc.diffIndex].Load(int3(samplePos, 0));
            w *= minHitDistWeight + RelaxComputeExponentialWeight(s.w, hitDistanceWeightParams.x, hitDistanceWeightParams.y);

            if (w > 1e-4) {
                sum += s * w;
                weightSum += w;
            }
        }

        float4 outDiff = sum / weightSum;
        outDiff.w = diffCenter.w;
        rdgStorageFloat4[pc.outDiffIndex][pixelPos] = outDiff;
    }

    // ---------------- Specular blur (anisotropic world-space kernel) ----------------
    // NRD: areaFactor = roughness * hitDistFactor * nonLinearAccumSpeed (roughness term replaces the spec-magic
    // curve in the radius). hitDistFactor uses de-normalized world hit distance WITHOUT *NoD ("hitDist * NoD
    // worsens denoising at glancing angles"). The kernel basis is stretched along the reflection direction.
    {
        float smc = RelaxGetSpecMagicCurve(centerRoughness);

        float worldHitDist = ReblurDenormHitDist(specCenter.w, centerViewZ, rc.gHitDistParams, centerRoughness);
        float hitDistFactor = RelaxGetHitDistFactor(worldHitDist, frustumSize);
        float areaFactor = centerRoughness * hitDistFactor * specNonLinearAccumSpeed;
        float blurRadius = ReblurGetBlurRadius(rc.gMaxBlurRadius, rc.gMinBlurRadius * smc, areaFactor, radiusScale);

        // Anisotropic basis: skew the kernel along the bent reflection direction. Stretch T (worldRadius * skew),
        // squeeze B (worldRadius / skew); skew tends to 1 (isotropic) at grazing angles, high roughness and as
        // the history converges, and is faded toward 1 for short hit distances (NRD spec spatial filter).
        float4 Dv = MLGetSpecularDominantDirection(centerNormal, V, centerRoughness);
        float NoD = abs(dot(centerNormal, Dv.xyz));
        float bentFactor = sqrt(saturate(hitDistFactor));
        float skewFactor = lerp(0.25 + 0.75 * centerRoughness, 1.0, NoD);
        skewFactor = lerp(skewFactor, 1.0, specNonLinearAccumSpeed);
        skewFactor = lerp(1.0, skewFactor, bentFactor);
        float3 bentD = normalize(lerp(centerNormal, Dv.xyz, bentFactor));
        float2x3 TB = ReblurGetKernelBasis(bentD, centerNormal);
        float worldRadius = RelaxPixelRadiusToWorld(rc.gUnproject, rc.gOrthoMode, blurRadius, centerViewZ);
        float3 T = TB[0] * (worldRadius * skewFactor);
        float3 B = TB[1] * (worldRadius / skewFactor);

        float2 geometryWeightParams = RelaxGetGeometryWeightParams(rc.gPlaneDistSensitivity, frustumSize, centerWorldPos, centerNormal);
        float normalWeightParam = ReblurGetNormalWeightParam(specNonLinearAccumSpeed, rc.gLobeAngleFraction, centerRoughness) / fractionScale;
        float2 roughnessWeightParams = RelaxGetRoughnessWeightParams(centerRoughness, saturate(rc.gRoughnessFraction * fractionScale));
        float2 hitDistanceWeightParams = ReblurGetHitDistanceWeightParams(specCenter.w, specNonLinearAccumSpeed, centerRoughness);
        float minHitDistWeight = rc.gMinHitDistanceWeight * fractionScale * smc * specNonLinearAccumSpeed;

        float4 sum = specCenter;
        float weightSum = 1.0;

        [unroll]
        for (int i = 0; i < 8; i++) {
            float3 offset = REBLUR_POISSON8[i];
            float2 rotated = MLRotateVector(rotator, offset.xy);
            float3 worldOffset = T * rotated.x + B * rotated.y;
            float2 uv = MLGetScreenUv(rc.gWorldToClip, centerWorldPos + worldOffset);
            float w = RelaxGetGaussianWeight(offset.z);
            if (RelaxIsInScreenNearest(uv) == 0.0) {
                uv = RelaxMirrorUv(uv);
                w = 1.0;
            }

            int2 samplePos = int2(uv * float2(rc.gRectSize));
            float sampleViewZ = RelaxLoadViewZ(pc.viewZIndex, samplePos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
            if (!RelaxIsInDenoisingRange(sampleViewZ, rc.gDenoisingRange)) { continue; }

            float4 sampleNR = RelaxUnpackNormalAndRoughness(rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(samplePos, 0)), viewToWorld);
            float3 sampleWorldPos = RelaxGetCurrentWorldPosFromPixelPos(samplePos, sampleViewZ, rc.gRectSizeInv, rc.gFrustumForward, rc.gFrustumRight, rc.gFrustumUp, rc.gOrthoMode);

            float NoX = dot(centerNormal, sampleWorldPos);
            w = RelaxApplyGeometryWeightAndRangeCheck(w, sampleViewZ, NoX, geometryWeightParams, rc.gDenoisingRange);
            float normalAngle = MLAcosApproxPositive(dot(centerNormal, sampleNR.xyz));
            w *= RelaxComputeWeight(normalAngle, normalWeightParam, 0.0);
            w *= RelaxComputeWeight(sampleNR.w, roughnessWeightParams.x, roughnessWeightParams.y);

            float4 s = rdgTexturesFloat4[pc.specIndex].Load(int3(samplePos, 0));
            w *= minHitDistWeight + RelaxComputeExponentialWeight(s.w, hitDistanceWeightParams.x, hitDistanceWeightParams.y);

            if (w > 1e-4) {
                sum += s * w;
                weightSum += w;
            }
        }

        float4 outSpec = sum / weightSum;
        outSpec.w = specCenter.w;
        rdgStorageFloat4[pc.outSpecIndex][pixelPos] = outSpec;
    }
}
