import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import limits_declarations;
import bindless_declarations;
import shader_functions;

import visibility_shading_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform VisibilityShadingPushConstant pc)
{
    ShadeDispatchParameters shadeParams = pc.shadeDispatchBuffer[pc.materialIndex];
    uint2 pixel = dtid.xy + uint2(shadeParams.minX, shadeParams.minY);
    if (any(pixel >= uint2(shadeParams.maxX, shadeParams.maxY))) {
        return;
    }

    bool onEdge = pixel.x == shadeParams.minX
               || pixel.x == shadeParams.maxX - 1
               || pixel.y == shadeParams.minY
               || pixel.y == shadeParams.maxY - 1;

    if (!onEdge) {
        return;
    }

    // Stable per-bucket color derived from materialIndex
    float hue = frac(float(pc.materialIndex) * 0.61803398875);
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    float4 color = float4(saturate(float3(r, g, b)), 1.0);

    rdgStorageUInt4[pc.gbufferOneIndex][pixel] = uint4(PackNormalOct16(float3(0, 0, 1)), 0u, PackPBR(0.5, 0.0), 1u);
    rdgStorageUInt2[pc.gbufferTwoIndex][pixel] = uint2(PackAlbedoRGB8(color), PackEmissiveRGBE(float3(0.0)));
}
