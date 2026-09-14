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
import rt_alpha_test_common;

#include "restir_features_macros.h"

static void WriteSunFlip(ReSTIRDISunPushConstant pc, uint2 pixel, uint field, float flip)
{
    rdgStorageFloat[pc.flipIndex][pixel] = flip;
    if (field != 0u) {
        int2 o = OppositeFieldPixel(pixel, field);
        if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
            rdgStorageFloat[pc.flipIndex][uint2(o)] = flip;
        }
    }
}

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
        if (pc.flipIndex != ~0u) { WriteSunFlip(pc, pixel, field, 0.0); }
        return;
    }

    uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    LightingInputs li = ReconstructLightingInputs(pixel, g1, pc.gbufferTwoIndex);

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float worldTexelSize = RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, 1.0);

    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    float3 shadowViewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv + scene.jitter * 0.5, viewSpaceDepth);
    float3 shadowWorldPos = mul(scene.invView, float4(shadowViewPosition, 1.0)).xyz + rdgTexturesFloat4[pc.shadowOriginOffsetIndex].Load(int3(pixel, 0)).xyz;
    float3 worldN = normalize(mul(float3x3(scene.invView), N));

    DirectionalLightData sun = pc.lightData->directionalLight;

    float2 candXi = SpatioTemporalBlueNoise2D(pixel, pc.frameIndex, 1u);
    float3 candDirW = SampleSunConeDirection(SunDirectionWorld(sun), sun.angularRadius, candXi.x, candXi.y);
    float3 candDirV = normalize(mul(float3x3(scene.view), candDirW));

    RTAlphaTestBuffers alphaBuffers = { pc.instanceBuffer, pc.primitiveBuffer, pc.materialBuffer, pc.indexBuffer, pc.vertexAttrBuffer };
    bool bAlphaTest = pc.bAlphaTest != 0u;
    bool bTraced = dot(N, candDirV) > 0.0;
    float blockerT = -1.0;
    if (bTraced) {
        blockerT = RestirSunShadowBlockerT(rdgAccelerationStructures[pc.tlasIndex], shadowWorldPos, worldN, candDirW, worldTexelSize, alphaBuffers, bAlphaTest);
    }
    bool bOccluded = blockerT >= 0.0;

    rdgStorageUInt[pc.visIndex][coord] = (bTraced && !bOccluded) ? EncodeSunVis(candXi) : SUN_VIS_EMPTY;

#if RESTIR_ENABLE_CONFIDENCE
    if (pc.flipIndex == ~0u) {
        return;
    }

    float flip = 0.0;
    bool bFlipLane = (field != 0u) || (((pixel.x + pixel.y + pc.frameIndex) & 1u) == 0u);
    if (bFlipLane && bTraced && pc.prevTlasIndex != ~0u) {
        float4 prevClip = mul(scene.prevViewProj, float4(worldPos, 1.0));
        float2 prevUv = (prevClip.xy / max(prevClip.w, 1e-6)) * float2(0.5, 0.5) + 0.5;
        float2 historyUv = uv + UnpackMotionVectors(g1.g) * 0.5;

        bool bStaticReceiver = all(abs(prevUv - historyUv) * float2(pc.renderExtent) < 1.0);
        if (bStaticReceiver) {
            bool bVisiblePrev = RestirSunShadowVisible(rdgAccelerationStructures[pc.prevTlasIndex], shadowWorldPos, worldN, candDirW, worldTexelSize, alphaBuffers, bAlphaTest);
            flip = (bVisiblePrev == bOccluded) ? 1.0 : 0.0;
        }
    }
    WriteSunFlip(pc, pixel, field, flip);
#endif
}
