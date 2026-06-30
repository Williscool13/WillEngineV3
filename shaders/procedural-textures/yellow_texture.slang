import constants_interop;
import push_constant_interop;
import procedural_texture_declarations;

[shader("compute")]
[numthreads(PROCEDURAL_TEXTURE_DISPATCH_X, PROCEDURAL_TEXTURE_DISPATCH_Y, 1)]
void ComputeYellowTexture(uniform ProceduralTextureBasePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    proceduralOutputTextures[pc.outputIndex].GetDimensions(size.x, size.y);
    if (tid.x >= size.x || tid.y >= size.y) { return; }

    proceduralOutputTextures[pc.outputIndex][tid.xy] = float4(1.0, 1.0, 0.0, 1.0);
}
