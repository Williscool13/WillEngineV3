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
