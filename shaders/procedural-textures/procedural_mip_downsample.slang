import constants_interop;
import constants_interop;
import push_constant_interop;

import procedural_texture_declarations;

[shader("compute")]
[numthreads(PROCEDURAL_TEXTURE_DISPATCH_X, PROCEDURAL_TEXTURE_DISPATCH_Y, 1)]
void ComputeProceduralMipDownsample(uniform ProceduralMipDownsamplePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 dstSize;
    proceduralOutputTextures[pc.dstIndex].GetDimensions(dstSize.x, dstSize.y);
    if (tid.x >= dstSize.x || tid.y >= dstSize.y) { return; }

    uint2 srcSize;
    proceduralOutputTextures[pc.srcIndex].GetDimensions(srcSize.x, srcSize.y);
    uint2 p0 = min(tid.xy * 2u, srcSize - 1u);
    uint2 p1 = min(tid.xy * 2u + 1u, srcSize - 1u);

    float4 sum = proceduralOutputTextures[pc.srcIndex][uint2(p0.x, p0.y)]
               + proceduralOutputTextures[pc.srcIndex][uint2(p1.x, p0.y)]
               + proceduralOutputTextures[pc.srcIndex][uint2(p0.x, p1.y)]
               + proceduralOutputTextures[pc.srcIndex][uint2(p1.x, p1.y)];
    proceduralOutputTextures[pc.dstIndex][tid.xy] = sum * 0.25f;
}

[shader("compute")]
[numthreads(PROCEDURAL_TEXTURE_DISPATCH_X, PROCEDURAL_TEXTURE_DISPATCH_Y, 1)]
void ComputeProceduralResize(uniform ProceduralMipDownsamplePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 dstSize;
    proceduralOutputTextures[pc.dstIndex].GetDimensions(dstSize.x, dstSize.y);
    if (tid.x >= dstSize.x || tid.y >= dstSize.y) { return; }

    uint2 srcSize;
    proceduralOutputTextures[pc.srcIndex].GetDimensions(srcSize.x, srcSize.y);
    float2 srcPos = (float2(tid.xy) + 0.5f) * float2(srcSize) / float2(dstSize) - 0.5f;
    float2 base = floor(srcPos);
    float2 f = srcPos - base;
    int2 lo = clamp(int2(base), int2(0, 0), int2(srcSize) - 1);
    int2 hi = clamp(int2(base) + 1, int2(0, 0), int2(srcSize) - 1);

    float4 c00 = proceduralOutputTextures[pc.srcIndex][uint2(lo.x, lo.y)];
    float4 c10 = proceduralOutputTextures[pc.srcIndex][uint2(hi.x, lo.y)];
    float4 c01 = proceduralOutputTextures[pc.srcIndex][uint2(lo.x, hi.y)];
    float4 c11 = proceduralOutputTextures[pc.srcIndex][uint2(hi.x, hi.y)];
    proceduralOutputTextures[pc.dstIndex][tid.xy] = lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}
