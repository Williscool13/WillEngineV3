/*
 * SIGMA shadow denoiser: temporal stabilization.
 * Reprojects the previous stabilized result by motion vector, clamps it to the current 3x3
 * neighborhood (rejecting stale history at disocclusions and moving shadow edges), and blends.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uniform SigmaTemporalPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 pixel = tid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;

    float2 cur = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).rg;
    float curVis = cur.r;

    if (depth <= 0.0 || pc.historyIndex == ~0u) {
        rdgStorageFloat2[pc.outputIndex][pixel] = cur;
        return;
    }

    int2 myTile = int2(pixel) / 16;
    float2 tile = rdgTexturesFloat2[pc.tilesIndex].Load(int3(myTile, 0)).rg;
    if (tile.g < 0.5 || tile.r > 0.5) {
        rdgStorageFloat2[pc.outputIndex][pixel] = cur;
        return;
    }

    // Current 3x3 neighborhood: min/max of visibility for history clamping, plus closest-depth
    // velocity (reversed-Z: larger depth is nearer) so foreground motion wins at silhouettes.
    float vmin = curVis;
    float vmax = curVis;
    float closestDepth = -1.0;
    uint closestPackedVelocity = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0)).g;
    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 sp = int2(pixel) + int2(dx, dy);
            if (any(sp < int2(0)) || any(sp >= int2(pc.renderExtent))) { continue; }
            float v = rdgTexturesFloat2[pc.shadowIndex].Load(int3(sp, 0)).r;
            vmin = min(vmin, v);
            vmax = max(vmax, v);
            float sd = rdgTexturesFloat[pc.depthIndex].Load(int3(sp, 0)).r;
            if (sd > closestDepth) { closestDepth = sd; closestPackedVelocity = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(sp, 0)).g; }
        }
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float2 velocity = UnpackMotionVectors(closestPackedVelocity) * 0.5;
    float2 historyUv = uv + velocity;

    float outVis = curVis;
    if (all(historyUv > float2(0.0)) && all(historyUv < float2(1.0)))
    {
        float histVis = rdgTexturesFloat2[pc.historyIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUv, 0).r;
        histVis = clamp(histVis, vmin, vmax);
        outVis = lerp(curVis, histVis, pc.historyWeight);
    }

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(outVis, cur.g);
}
