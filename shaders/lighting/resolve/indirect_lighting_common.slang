module indirect_lighting_common;

import constants_interop;
import bindless_declarations;
import shader_functions;
import pbr_functions;
import ddgi_interop;
import ddgi_common;
import reflection_probe_interop;
import reflection_probe_common;

public struct IndirectLightingInputs
{
    public uint2 pixel;
    public float3 viewPosition;
    public float3 viewN;
    public float3 viewV;
    public float roughness;
    public float3 F0;
    public float3 F_ibl;
    public float2 envBRDF;
    public float gtao;
    public float preExposure;
    public float4x4 invView;
    public float3 cameraWorldPos;
    public int skyboxIndex;
    public float iblIntensity;
    /** ~0u: no gather, diffuse comes from the probe chain. */
    public uint giResolvedIndex;
    /** ~0u: sky occlusion falls back to the diffuse-to-sky irradiance ratio. */
    public uint skyVisIndex;
    /** ~0u: no traced reflection for this pixel. */
    public uint reflectionIndex;
    public bool bDDGI;
    public DDGICascadeSetGPU* ddgiCascades;
    public uint reflectionProbeCount;
    public ReflectionProbeGPU* reflectionProbes;
    public uint* worldGridProbeGrid;
};

public struct IndirectLighting
{
    /** Irradiance, before albedo, kD and GTAO. */
    public float3 diffuse;
    /** Final specular term. */
    public float3 specular;
};

/**
 * Diffuse: gather, else sky with DDGI over it and reflection probes over what DDGI left uncovered.
 * Specular: traced reflection without GTAO, else occluded sky with reflection probes over it, times GTAO.
 */
public IndirectLighting ResolveIndirectLighting(IndirectLightingInputs in)
{
    IndirectLighting result;
    result.diffuse = float3(0.0);
    result.specular = float3(0.0);

    SamplerState linearSampler = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
    float3 worldPosition = mul(in.invView, float4(in.viewPosition, 1.0)).xyz;
    float3 worldN = normalize(mul((float3x3)in.invView, in.viewN));

    float3 skyIrradiance = float3(0.0);
    if (in.skyboxIndex >= 0) {
        skyIrradiance = DiffuseIrradiance(assetCubemaps[in.skyboxIndex], linearSampler, worldN) * in.iblIntensity;
    }

    ReflectionProbeBlend probeBlend = {};
    probeBlend.indexA = -1;
    if (in.reflectionProbeCount > 0u) {
        probeBlend = PickReflectionProbeBinned(worldPosition, in.reflectionProbes, in.reflectionProbeCount, in.worldGridProbeGrid, in.cameraWorldPos);
    }

    if (in.giResolvedIndex != ~0x0u) {
        result.diffuse = rdgTexturesFloat4[in.giResolvedIndex].Load(int3(in.pixel, 0)).rgb / in.preExposure;
    } else {
        result.diffuse = skyIrradiance;
        float giW = 0.0;
        if (in.bDDGI) {
            float3 cameraDirection = normalize(mul((float3x3)in.invView, in.viewPosition));
            float edgeFade;
            float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, in.ddgiCascades, length(in.viewPosition), edgeFade);
            result.diffuse = gi.rgb * gi.w + result.diffuse * (1.0 - max(gi.w, edgeFade));
            giW = gi.w;
        }
        if (probeBlend.indexA >= 0) {
            float3 probeIrradiance = SampleReflectionProbeIrradianceBlended(probeBlend, in.reflectionProbes, worldN);
            result.diffuse = lerp(result.diffuse, probeIrradiance, probeBlend.coverage * (1.0 - giW));
        }
    }

    // Select, don't lerp: a traced ray already resolves sky visibility and occlusion, so it replaces the fallback outright and skips GTAO.
    if (in.reflectionIndex != ~0x0u) {
        result.specular = rdgTexturesFloat4[in.reflectionIndex].Load(int3(in.pixel, 0)).rgb / in.preExposure * (in.F0 * in.envBRDF.x + in.envBRDF.y);
        return result;
    }

    if (in.skyboxIndex >= 0) {
        result.specular = SpecularReflection(assetCubemaps[in.skyboxIndex], linearSampler, in.viewV, in.viewN, in.invView, in.roughness, in.F_ibl, in.envBRDF) * in.iblIntensity;
        float skyOcclusion;
        if (in.skyVisIndex != ~0x0u) {
            skyOcclusion = rdgTexturesFloat2[in.skyVisIndex].Load(int3(in.pixel, 0)).x;
        } else {
            skyOcclusion = saturate(Luma(result.diffuse) / max(Luma(skyIrradiance), 1e-4));
        }
        result.specular *= skyOcclusion;
    }
    if (probeBlend.indexA >= 0) {
        float3 worldR = normalize(mul((float3x3)in.invView, reflect(-in.viewV, in.viewN)));
        float3 probeRadiance = SampleReflectionProbeSpecularBlended(probeBlend, in.reflectionProbes, worldPosition, worldR, in.roughness);
        result.specular = lerp(result.specular, probeRadiance * (in.F_ibl * in.envBRDF.x + in.envBRDF.y), probeBlend.coverage);
    }
    result.specular *= in.gtao;
    return result;
}
