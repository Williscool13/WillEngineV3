/*
 * Directional-sun visibility: one uniform cone sample over the sun disk plus one anyhit shadow ray per
 * pixel per frame. The target pdf is flat over the disk, so a single sample is already a complete
 * estimator; there is nothing for RIS to select and nothing for temporal or spatial reuse to carry except
 * the visibility bit, and smearing that belongs to the denoiser. The cone pdf cancels the solid-angle
 * divide between illuminance and radiance, so the resolve shades with plain illuminance times NoL.
 * Output: SUN_VIS_EMPTY when shadowed, facing away or unshaded, else the unorm16x2 cone sample,
 * re-anchored to the current sun axis at decode so sun rotation needs no invalidation.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;

import bindless_declarations;
import restir_common;
import random_common;

#include "restir_features_macros.h"

// Blocker-distance drift as a pseudo-flip for the confidence signal: coherent occluder motion along the
// ray survives the gradient's stratum mean, stochastic penumbra noise cancels.
static const float BLOCKER_SENSITIVITY = 4.0;
static const float BLOCKER_NORM_BASE = 1.0;
static const float BLOCKER_FRACTION_ALPHA = 0.125;
static const float BLOCKER_DIST_ALPHA = 0.25;
static const float BLOCKER_TRUST_FRACTION = 0.25;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReSTIRDISun(uniform ReSTIRDISunPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint field = pc.activeCheckerboardField;
    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint2 coord = threadID.xy;
    if (coord.x >= stride || coord.y >= pc.renderExtent.y) {
        return;
    }
    uint2 pixel = ReservoirPosToPixelPos(coord, field);
    if (pixel.x >= pc.renderExtent.x) {
        // Odd-width phantom column: past extent but still in the aliased target, keep it initialized.
        rdgStorageUInt[pc.visIndex][coord] = SUN_VIS_EMPTY;
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        rdgStorageUInt[pc.visIndex][coord] = SUN_VIS_EMPTY;
        return;
    }

    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    LightingInputs li = ReconstructLightingInputs(pixel, g1, pc.gbufferTwoIndex);

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, abs(dot(N, V)));

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 worldN = normalize(mul(float3x3(scene.invView), N));

    DirectionalLightData sun = pc.lightData->directionalLight;

    float2 candXi = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 1u);
    float3 candDirW = SampleSunConeDirection(SunDirectionWorld(sun), sun.angularRadius, candXi.x, candXi.y);
    float3 candDirV = normalize(mul(float3x3(scene.view), candDirW));

    bool bTraced = dot(N, candDirV) > 0.0;
    float blockerT = -1.0;
    if (bTraced) {
        blockerT = RestirSunShadowBlockerT(rdgAccelerationStructures[pc.tlasIndex], worldPos, worldN, candDirW, worldTexelSize);
    }
    bool bOccluded = blockerT >= 0.0;

    rdgStorageUInt[pc.visIndex][coord] = (bTraced && !bOccluded) ? EncodeSunVis(candXi) : SUN_VIS_EMPTY;

#if RESTIR_ENABLE_CONFIDENCE
    if (pc.signalIndex == ~0u && pc.blockerIndex == ~0u) {
        return;
    }

    // Reprojection exists only to fetch the blocker-distance accumulator; the visibility result itself never reuses history.
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 historyUV = uv + velocityNDC * 0.5;
    bool bHistoryUsable = false;
    uint2 historyPixel = uint2(0, 0);

    if (pc.prevGbufferOneIndex != ~0u && all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0))) {
        uint2 tapPixel = clamp(uint2(historyUV * float2(pc.renderExtent)), uint2(0, 0), pc.renderExtent - uint2(1, 1));
        bool bInBounds = true;
        if (field != 0u) {
            int2 snapped = ActivateCheckerboardPixel(int2(tapPixel), true, field);
            bInBounds = all(snapped >= int2(0, 0)) && all(snapped < int2(pc.renderExtent));
            if (bInBounds) { tapPixel = uint2(snapped); }
        }
        if (bInBounds) {
            float NoV = abs(dot(N, normalize(viewPosition)));
            float frustumSize = abs(scene.ndcToViewMul.y) * abs(viewSpaceDepth);
            float disocclusionThreshold = saturate(0.01 / lerp(0.05, 1.0, NoV)) * frustumSize;
            float expectedPrevViewDepth = viewSpaceDepth + UnpackViewZDelta(g1.w);

            float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.prevGbufferOneIndex].Load(int3(tapPixel, 0)).r));
            float3 historyWorldN = normalize(mul(transpose(float3x3(scene.prevView)), historyN));
            if (dot(worldN, historyWorldN) >= 0.5) {
                float historyRawDepth = rdgTexturesFloat[pc.prevDepthIndex].Load(int3(tapPixel, 0)).r;
                float historyViewDepth = ScreenToViewSpaceDepth(historyRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                if (abs(historyViewDepth - expectedPrevViewDepth) * NoV <= disocclusionThreshold) {
                    historyPixel = tapPixel;
                    bHistoryUsable = true;
                }
            }
        }
    }

    if (!bTraced) {
        if (pc.blockerIndex != ~0u) {
            // No trace this frame: decay the occluded fraction, keep the accumulated distance.
            float2 prevAcc = (bHistoryUsable && pc.prevBlockerIndex != ~0u) ? rdgTexturesFloat2[pc.prevBlockerIndex].Load(int3(historyPixel, 0)).rg : float2(0.0, 0.0);
            if (any(isnan(prevAcc)) || any(isinf(prevAcc))) { prevAcc = float2(0.0, 0.0); }
            rdgStorageFloat2[pc.blockerIndex][pixel] = float2(prevAcc.x, prevAcc.y * (1.0 - BLOCKER_FRACTION_ALPHA));
        }
        return;
    }

    float roughness = max(li.roughness, RESTIR_MIN_ROUGHNESS);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float pSun = EvalSunPHat(candDirV, V, N, li.albedo.xyz, F0, li.metallic, roughness, SunIlluminance(sun));
    float winnerVis = bOccluded ? 0.0 : 1.0;

    float blockerFlip = 0.0;
    if (pc.blockerIndex != ~0u) {
        float2 prevAcc = float2(0.0, 0.0);
        bool bAccValid = false;
        if (bHistoryUsable && pc.prevBlockerIndex != ~0u) {
            prevAcc = rdgTexturesFloat2[pc.prevBlockerIndex].Load(int3(historyPixel, 0)).rg;
            if (!any(isnan(prevAcc)) && !any(isinf(prevAcc))) { bAccValid = true; }
        }
        float accT = bAccValid ? prevAcc.x : 0.0;
        float fr = bAccValid ? prevAcc.y : 0.0;
        float frNew = lerp(fr, bOccluded ? 1.0 : 0.0, bAccValid ? BLOCKER_FRACTION_ALPHA : 1.0);
        float accTNew = accT;
        if (bOccluded) {
            // Delta vs the ACCUMULATED blocker distance, and only where occluded history exists:
            // raw-vs-raw at 1spp is a noise detector, and lit<->shadowed transitions belong to the
            // visibility-flip term below, so unshadowed samples need no sentinel here.
            if (bAccValid && fr > BLOCKER_TRUST_FRACTION) {
                blockerFlip = clamp(BLOCKER_SENSITIVITY * (blockerT - accT) / (abs(viewSpaceDepth) + BLOCKER_NORM_BASE), -1.0, 1.0);
            }
            accTNew = (bAccValid && fr > 0.0) ? lerp(accT, blockerT, BLOCKER_DIST_ALPHA) : blockerT;
        }
        rdgStorageFloat2[pc.blockerIndex][pixel] = float2(accTNew, frNew);
    }

    if (pc.signalIndex != ~0u) {
        float visPrev = winnerVis;
        if (pc.prevTlasIndex != ~0u) {
            visPrev = RestirSunShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], worldPos, worldN, candDirW, worldTexelSize) ? 1.0 : 0.0;
        }
        float sunFlip = clamp((winnerVis - visPrev) + blockerFlip, -1.0, 1.0);
        float sunMag = max(max(winnerVis, visPrev), abs(blockerFlip));
        float2 sunSig = float2(pSun * sunFlip, pSun * sunMag);

        rdgStorageFloat2[pc.signalIndex][pixel] = rdgStorageFloat2[pc.signalIndex][pixel] + sunSig;
        if (field != 0u) {
            int2 o = OppositeFieldPixel(pixel, field);
            if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
                rdgStorageFloat2[pc.signalIndex][uint2(o)] = rdgStorageFloat2[pc.signalIndex][uint2(o)] + sunSig;
            }
        }
    }
#endif
}
