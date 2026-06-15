/*
 * SIGMA shadow denoiser: penumbra-aware spatial filter (blur + post-blur).
 * passIndex 0 (blur): PCSS-style blocker search estimates the penumbra (closest-hit distance * tan(sun angular radius)), then bilaterally blurs the noisy 1spp visibility with a kernel sized to that penumbra.
 * passIndex 1 (post-blur): reuses the penumbra stored by pass 0 and re-blurs with a decorrelated kernel.
 * The blur kernel is oriented in the surface tangent plane (view space) and projected to screen, so it foreshortens correctly on grazing surfaces instead of being a flat screen-space disc.
 */
import common_interop;
import push_constant_interop;
import lights_interop;
import constants_interop;
import bindless_declarations;
import packing_functions;
import shader_functions;
import shader_constants;
import relax_utils;
import random_common;

static float2 Rotate(float2 v, float2 rotor)
{
    return float2(v.x * rotor.x - v.y * rotor.y, v.x * rotor.y + v.y * rotor.x);
}

/** Inverse of CheapReconstructViewSpacePosition: view-space position -> screen uv. */
static float2 ViewToScreenUv(float3 viewPos, float2 ndcToViewMul, float2 ndcToViewAdd)
{
    float vz = -viewPos.z;
    float2 s = float2(viewPos.x, -viewPos.y) / max(vz, 1e-6);
    return (s - ndcToViewAdd) / ndcToViewMul;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uniform SigmaBlurPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 pixel = tid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
        return;
    }

    float2 center = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).rg;
    float centerVis = center.r;
    float centerHit = center.g; // pass 0: raw closest-occluder distance; pass 1: penumbra (world) from pass 0

    int2 myTile = int2(pixel) / 16;
    float2 tile = rdgTexturesFloat2[pc.tilesIndex].Load(int3(myTile, 0)).rg;
    if (tile.g < 0.5) { rdgStorageFloat2[pc.outputIndex][pixel] = float2(0.0, 0.0); return; }
    if (tile.r > 0.5) { rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0); return; }

    float centerViewZ = RelaxLoadViewZ(pc.depthIndex, int2(pixel), scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0)).r));
    float pixelWorldSize = abs(scene.ndcToViewMul.y) * centerViewZ / float(pc.renderExtent.y);

    float angle = BlueNoiseAngle(pixel, pc.frameIndex + pc.passIndex * 32u);
    float2 rotor = float2(cos(angle), sin(angle));

    // Penumbra in world units (already includes penumbraScale).
    float penumbraWorld;
    if (pc.passIndex == 0u)
    {
        float tanAngle = tan(pc.lightData->directionalLight.angularRadius);
        float blockerSum = (centerVis < 0.5) ? centerHit : 0.0;
        float blockerCount = (centerVis < 0.5) ? 1.0 : 0.0;
        [unroll]
        for (int i = 0; i < 16; i++)
        {
            float2 unitOffset = POISSON_DISK_16[i] * 2.0;
            int2 sp = int2(pixel) + int2(Rotate(unitOffset, rotor) * pc.blockerSearchPixels);
            if (any(sp < int2(0)) || any(sp >= int2(pc.renderExtent))) { continue; }
            float2 s = rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).rg;
            if (s.r < 0.5) { blockerSum += s.g; blockerCount += 1.0; }
        }
        penumbraWorld = blockerCount > 0.0 ? (blockerSum / blockerCount) * tanAngle * pc.penumbraScale : 0.0;
    }
    else
    {
        penumbraWorld = centerHit;
    }

    if (penumbraWorld <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(centerVis, 0.0);
        return;
    }

    // Cap the world radius so its screen projection stays within maxKernelPixels (cost/cache bound).
    float worldRadius = min(penumbraWorld, pc.maxKernelPixels * pixelWorldSize);

    // Oriented tangent-plane kernel: build a view-space basis from the surface normal, offset within the
    // surface plane, and project each tap back to screen so the kernel foreshortens on grazing surfaces.
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float3 centerViewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
    float3x3 basis = MLGetBasis(centerN);
    float3 T = basis[0];
    float3 B = basis[1];

    float sum = centerVis;
    float wsum = 1.0;
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 o = Rotate(POISSON_DISK_16[i] * 2.0, rotor);
        float3 sampleView = centerViewPos + (T * o.x + B * o.y) * worldRadius;
        float2 sUv = ViewToScreenUv(sampleView, scene.ndcToViewMul, scene.ndcToViewAdd);
        if (any(sUv <= 0.0) || any(sUv >= 1.0)) { continue; }
        int2 sp = int2(sUv * float2(pc.renderExtent));
        if (any(sp < int2(0)) || any(sp >= int2(pc.renderExtent))) { continue; }

        float sd = rdgTexturesFloat[pc.depthIndex].Load(int3(sp, 0)).r;
        if (sd <= 0.0) { continue; }

        float sViewZ = RelaxLoadViewZ(pc.depthIndex, sp, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 sN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(sp, 0)).r));

        float wz = RelaxGetBilateralWeight(sViewZ, centerViewZ);
        float wn = pow(max(dot(centerN, sN), 0.0), pc.normalWeightPower);
        float wg = RelaxGetGaussianWeight(length(o));
        float w = wz * wn * wg;

        sum += rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).r * w;
        wsum += w;
    }

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(sum / max(wsum, 1e-6), penumbraWorld);
}
