/*
 * SIGMA shadow denoiser: penumbra-aware spatial filter.
 * PCSS-style: search for blockers to estimate the penumbra (closest-hit distance * tan(sun angular radius)), then bilaterally blur the noisy 1spp visibility with a kernel sized to that penumbra.
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

static const float SIGMA_MAX_KERNEL_PIXELS = 32.0;

static float2 Rotate(float2 v, float2 rotor)
{
    return float2(v.x * rotor.x - v.y * rotor.y, v.x * rotor.y + v.y * rotor.x);
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

    float2 center = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).rg; // (visibility, closest-occluder distance)
    float centerVis = center.r;
    float centerHit = center.g;

    float centerViewZ = RelaxLoadViewZ(pc.depthIndex, int2(pixel), scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0)).r));

    float tanAngle = tan(pc.lightData->directionalLight.angularRadius);
    float pixelWorldSize = abs(scene.ndcToViewMul.y) * centerViewZ / float(pc.renderExtent.y);

    uint seed = SeedRng(pixel, pc.frameIndex, 0x51ed27a1u);
    float rot = HashStep(seed) * 6.28318530718;
    float2 rotor = float2(cos(rot), sin(rot));

    // Blocker search: average occluder distance among shadowed samples within a fixed search radius.
    float blockerSum = (centerVis < 0.5) ? centerHit : 0.0;
    float blockerCount = (centerVis < 0.5) ? 1.0 : 0.0;
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 unitOffset = POISSON_DISK_16[i] * 2.0;
        int2 sp = int2(pixel) + int2(Rotate(unitOffset, rotor) * SIGMA_MAX_KERNEL_PIXELS);
        if (any(sp < int2(0)) || any(sp >= int2(pc.renderExtent))) { continue; }
        float2 s = rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).rg;
        if (s.r < 0.5) { blockerSum += s.g; blockerCount += 1.0; }
    }

    float penumbraWorld = blockerCount > 0.0 ? (blockerSum / blockerCount) * tanAngle : 0.0;
    if (penumbraWorld <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(centerVis, centerHit);
        return;
    }

    float kernelPixels = clamp(penumbraWorld / max(pixelWorldSize, 1e-6), 1.0, SIGMA_MAX_KERNEL_PIXELS);

    // Bilateral blur of visibility, weighted by depth, normal, and a Gaussian falloff.
    float sum = centerVis;
    float wsum = 1.0;
    [unroll]
    for (int i = 0; i < 16; i++)
    {
        float2 unitOffset = POISSON_DISK_16[i] * 2.0;
        int2 sp = int2(pixel) + int2(Rotate(unitOffset, rotor) * kernelPixels);
        if (any(sp < int2(0)) || any(sp >= int2(pc.renderExtent))) { continue; }

        float sd = rdgTexturesFloat[pc.depthIndex].Load(int3(sp, 0)).r;
        if (sd <= 0.0) { continue; }

        float sViewZ = RelaxLoadViewZ(pc.depthIndex, sp, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 sN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(sp, 0)).r));

        float wz = RelaxGetBilateralWeight(sViewZ, centerViewZ);
        float wn = pow(max(dot(centerN, sN), 0.0), 16.0);
        float wg = RelaxGetGaussianWeight(length(unitOffset));
        float w = wz * wn * wg;

        sum += rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).r * w;
        wsum += w;
    }

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(sum / max(wsum, 1e-6), penumbraWorld);
}
