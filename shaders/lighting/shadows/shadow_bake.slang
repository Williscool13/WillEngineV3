import common_interop;
import push_constant_interop;
import bindless_declarations;

static float LinearizeDepth(float reverseZ, float nearPlane, float farPlane)
{
    const float viewDepth = nearPlane * farPlane / (reverseZ * (farPlane - nearPlane) + nearPlane);
    return saturate((viewDepth - nearPlane) / (farPlane - nearPlane));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeShadowBakeLinearize(uniform ShadowBakeLinearizePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint halfWidth = pc.resolution / 2;
    if (dtid.x >= halfWidth || dtid.y >= pc.resolution) {
        return;
    }
    const int2 texel = int2(dtid.x * 2, dtid.y);
    const float a = LinearizeDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(texel, 0)).r, pc.nearPlane, pc.farPlane);
    const float b = LinearizeDepth(rdgTexturesFloat[pc.depthIndex].Load(int3(texel + int2(1, 0), 0)).r, pc.nearPlane, pc.farPlane);
    pc.output[dtid.y * halfWidth + dtid.x] = uint(a * 65535.0 + 0.5) | (uint(b * 65535.0 + 0.5) << 16);
}
