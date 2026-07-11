module gi_gather_common;

import bindless_declarations;
import constants_interop;

/** Bilinear-samples the half-res gather SH, evaluated at the pixel's world normal; returns indirectDiffuse (E/pi). */
public float3 GIGatherIndirectDiffuse(uint shRIndex, uint shGIndex, uint shBIndex, float2 uv, float3 worldN)
{
    float4 shR = rdgTexturesFloat4[shRIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    float4 shG = rdgTexturesFloat4[shGIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    float4 shB = rdgTexturesFloat4[shBIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);
    return max(float3(shR.x + dot(shR.yzw, worldN), shG.x + dot(shG.yzw, worldN), shB.x + dot(shB.yzw, worldN)), float3(0.0));
}

/** Debug views: 2 = raw gather irradiance, 3 = fallback tier (green cache / blue probe / yellow sky / red backface), 4 = hit distance. */
public float3 GIGatherDebugColor(uint dataIndex, uint2 pixel, uint mode, float3 indirectDiffuse)
{
    if (mode == 2u) {
        return indirectDiffuse;
    }
    float2 data = rdgTexturesFloat2[dataIndex].Load(int3(pixel >> 1u, 0));
    if (mode == 3u) {
        switch (uint(data.y)) {
            case 0u: return float3(0.1, 0.9, 0.1);
            case 1u: return float3(0.15, 0.3, 1.0);
            case 2u: return float3(1.0, 0.9, 0.2);
            case 3u: return float3(1.0, 0.1, 0.1);
            default: return float3(0.0);
        }
    }
    return float3(data.x / 20.0);
}
