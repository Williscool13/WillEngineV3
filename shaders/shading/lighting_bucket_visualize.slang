import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingBucketVisualize(uint3 dtid : SV_DispatchThreadID, uniform LightingBucketVisualizePushConstant pc)
{
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = dtid.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    bool onEdge = pixel.x == lightParams.minX
               || pixel.x == lightParams.maxX - 1
               || pixel.y == lightParams.minY
               || pixel.y == lightParams.maxY - 1;

    if (!onEdge) {
        return;
    }

    // Stable per-bucket color derived from lightingIndex
    float hue = frac(float(pc.lightingIndex) * 0.61803398875 + 0.5);
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    float4 color = float4(saturate(float3(r, g, b)), 1.0);

    uint packedNormal = PackNormalOct16(float3(0, 0, 1));
    uint packedPBR = PackPBR(0.5, 0.0);
    uint packedAlbedo = PackAlbedoRGBA8(color);
    uint packedEmissive = PackEmissiveRGBE(float3(0.0));

    rdgStorageUInt4[pc.gbufferOneIndex][pixel] = uint4(packedNormal, 0u, packedPBR, 1u);
    rdgStorageUInt2[pc.gbufferTwoIndex][pixel] = uint2(packedAlbedo, packedEmissive);
}
