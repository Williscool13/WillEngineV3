/*
 * SIGMA shadow denoiser: tile classification (mirrors NRD SIGMA_ClassifyTiles).
 * Reduces each 16x16 tile to (needsFilter, maxBlurRadius/16, isSky). "needsFilter" is 0 for tiles that
 * are uniformly lit or uniformly umbra/sky/hard-shadow (nothing to soften) and 1 for tiles containing a
 * soft penumbra. maxBlurRadius drives the dilation width in the smooth pass. Output RGBA8.
 */
import common_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;
import denoiser_common;
import sigma_common;

groupshared uint s_Mask;
groupshared uint s_Radius;

[shader("compute")]
[numthreads(8, 4, 1)]
void ComputeSigmaClassifyTiles(uniform SigmaClassifyPushConstant pc, uint2 threadPos : SV_GroupThreadID, uint2 tilePos : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    if (threadIndex == 0) {
        s_Mask = 0;
        s_Radius = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 basePixel = tilePos * 16 + threadPos * uint2(2, 4);

    uint mask = 0;
    float maxRadius = 0.0;

    [unroll]
    for (uint i = 0; i < 2; i++)
    {
        [unroll]
        for (uint j = 0; j < 4; j++)
        {
            int2 pos = min(int2(basePixel) + int2(i, j), int2(pc.renderExtent) - 1);
            float penumbra = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pos, 0)).g;
            float rawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pos, 0)).r;

            bool isInf = rawDepth <= 0.0;
            bool isShadow = penumbra == 0.0;
            bool isLit = SigmaIsLit(penumbra);

            mask += ((isLit || isInf || isShadow) ? 1u : 0u) << 0;
            mask += ((!isLit || isInf || isShadow) ? 1u : 0u) << 9;
            mask += (isInf ? 1u : 0u) << 18;

            float hitDist = (isLit || isInf) ? 0.0 : penumbra;
            float viewZ = ScreenToViewSpaceDepth(rawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float pixelSize = SigmaPixelSize(scene.ndcToViewMul, viewZ, pc.renderExtent.y);
            float pixelRadius = SigmaGetKernelRadiusInPixels(hitDist, pixelSize, pc.maxKernelPixels);

            maxRadius = max(pixelRadius, maxRadius);
        }
    }

    InterlockedAdd(s_Mask, mask);
    InterlockedMax(s_Radius, asuint(maxRadius));
    GroupMemoryBarrierWithGroupSync();

    if (threadIndex == 0)
    {
        bool isLit = ((s_Mask >> 0) & 511u) == 256u;
        bool isUmbra = ((s_Mask >> 9) & 511u) == 256u;
        bool isInf = ((s_Mask >> 18) & 511u) == 256u;

        float4 result;
        result.x = (isLit || isUmbra) ? 0.0 : 1.0;
        result.y = saturate(asfloat(s_Radius) / 16.0);
        result.z = isInf ? 1.0 : 0.0;
        result.w = 0.0;

        rdgStorageFloat4[pc.outputIndex][tilePos] = result;
    }
}
