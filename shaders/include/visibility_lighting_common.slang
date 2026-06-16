module visibility_lighting_common;

import constants_interop;
import model_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;

public struct LightingInputs
{
    public bool valid;
    public float3 viewNormal;
    public float roughness;
    public float metallic;
    public float4 albedo;
    public float3 emissiveRGB;
    public float2 shadows;
}

public LightingInputs ReconstructLightingInputs(
    uint2 pixel,
    uint32_t visibilityBufferIndex,
    uint32_t lightingIndex,
    Instance* instanceBuffer,
    uint32_t gbufferOneIndex,
    uint32_t gbufferTwoIndex,
    uint32_t shadowsIndex
)
{
    LightingInputs result;
    result.valid = false;

    if (lightingIndex != ~0x0) {
        uint2 vis = rdgTexturesUInt2[visibilityBufferIndex].Load(int3(pixel, 0)).xy;
        if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
            return result;
        }

        uint instanceIndex = vis.y;
        Instance instance = instanceBuffer[instanceIndex];
        if (lightingIndex != ~0u && instance.lightingIndex != lightingIndex) {
            return result;
        }
    }

    uint4 gt1 = rdgTexturesUInt4[gbufferOneIndex].Load(int3(pixel, 0));
    uint2 gt2 = rdgTexturesUInt2[gbufferTwoIndex].Load(int3(pixel, 0));

    result.viewNormal = UnpackNormalOct16(gt1.r);
    UnpackPBR(gt1.b, result.roughness, result.metallic);
    result.albedo = UnpackAlbedoRGB8(gt2.r);
    result.emissiveRGB = UnpackEmissiveRGBE(gt2.g);

    result.shadows = float2(1.0f);
    if (shadowsIndex != ~0x0u) {
        result.shadows = rdgTexturesFloat2[shadowsIndex].Load(int3(pixel, 0)).xy;
    }

    result.valid = true;
    return result;
}
