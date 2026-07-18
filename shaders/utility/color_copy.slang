import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeColorCopy(uint3 dtid : SV_DispatchThreadID, uniform ColorCopyPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extents)) {
        return;
    }

    float4 color = rdgTexturesFloat4[pc.srcIndex].Load(int3(pixel, 0));
    rdgStorageFloat4[pc.dstIndex][pixel] = color;
}
