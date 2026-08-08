import push_constant_interop;
import bindless_declarations;

groupshared float gHiZTile[16][16];

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeHiZBuild(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uniform HiZBuildPushConstant pc)
{
    // <= 5 level depth prefilter following XeGTAO example
    uint2 pixel = dtid.xy;
    float depth = 1.0;
    if (all(pixel < pc.dstExtent)) {
        uint2 sBegin = pixel * pc.srcExtent / pc.dstExtent;
        uint2 sEnd = (pixel + 1u) * pc.srcExtent / pc.dstExtent;
        for (uint sy = sBegin.y; sy < sEnd.y; sy++) {
            for (uint sx = sBegin.x; sx < sEnd.x; sx++) {
                float d = pc.bSampledSrc != 0u ? rdgTexturesFloat[pc.srcIndex].Load(int3(sx, sy, 0)) : rdgStorageFloat[pc.srcIndex][uint2(sx, sy)];
                depth = min(depth, d);
            }
        }
        rdgStorageFloat[pc.dstIndex[0]][pixel] = depth;
    }
    gHiZTile[gtid.y][gtid.x] = depth;

    uint2 levelExtent = pc.dstExtent;
    for (uint level = 1; level < pc.dstMipCount; level++) {
        GroupMemoryBarrierWithGroupSync();
        levelExtent = max(levelExtent >> 1u, uint2(1u, 1u));
        uint stride = 1u << level;
        if (all((gtid.xy & (stride - 1u)) == 0u)) {
            uint halfStride = stride >> 1u;
            float d = min(min(gHiZTile[gtid.y][gtid.x], gHiZTile[gtid.y][gtid.x + halfStride]),
                          min(gHiZTile[gtid.y + halfStride][gtid.x], gHiZTile[gtid.y + halfStride][gtid.x + halfStride]));
            gHiZTile[gtid.y][gtid.x] = d;
            uint2 dstPixel = pixel >> level;
            if (all(dstPixel < levelExtent)) {
                rdgStorageFloat[pc.dstIndex[level]][dstPixel] = d;
            }
        }
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeHiZDebug(uint3 dtid : SV_DispatchThreadID, uniform HiZDebugPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.outExtent)) {
        return;
    }

    uint2 mipPixel = min(pixel * pc.mipExtent / pc.outExtent, pc.mipExtent - 1u);
    float depth = rdgTexturesFloat[pc.hizIndex].Load(int3(mipPixel, pc.mip));
    float gray = pow(saturate(depth), 0.25);
    rdgStorageFloat4[pc.outputIndex][pixel] = float4(gray, gray, gray, 1.0);
}
