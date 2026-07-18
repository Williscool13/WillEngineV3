import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeDepthCopy(uint3 dtid : SV_DispatchThreadID, uniform DepthCopyPushConstant pc)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.extents)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0));
    rdgStorageFloat[pc.outputIndex][pixel] = depth;
}
