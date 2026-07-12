import common_interop;
import push_constant_interop;
import model_interop;
import ddgi_interop;
import ddgi_common;
import instance_mask_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import pbr_functions;
import random_common;
import hit_shading_common;
import world_cache_hash;
import world_cache_interop;
import gi_gather_common;

// =============== Shader Summary ============
// One cosine-weighted ray per half-res pixel, zero shading at the hit: cache radiance, else albedo * DDGI irradiance (one material fetch), skybox on miss. MC-projected into per-channel 2-band SH, evaluated at the full-res normal at composite.

static const float GI_GATHER_MAX_RADIANCE = 20.0;
static const float GI_GATHER_SCREEN_MIN_ROUGHNESS = 0.3;

static const uint GI_GATHER_TIER_SCREEN = 0u;
static const uint GI_GATHER_TIER_CACHE = 1u;
static const uint GI_GATHER_TIER_PROBE = 2u;
static const uint GI_GATHER_TIER_SKY = 3u;
static const uint GI_GATHER_TIER_BACKFACE = 4u;
static const uint GI_GATHER_TIER_INVALID = 5u;

static float3 SampleCosineHemisphere(float3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(r * cos(phi) * t + r * sin(phi) * b + sqrt(max(1.0 - u1, 0.0)) * n);
}

struct GatherHitSurface
{
    float3 normal;
    float3 diffuseColor;
    float3 emissive;
};

static GatherHitSurface DecodeGatherHit(GIGatherPushConstant pc, Instance instance, uint triangleIndex, float2 barycentrics)
{
    Primitive primitive = pc.primitiveBuffer[instance.primitiveIndex];
    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = pc.vertexAttrBuffer[pc.indexBuffer[firstIndex + 2]];
    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    float3 normalOS = normalize(bary.x * DecodeNormal(a0) + bary.y * DecodeNormal(a1) + bary.z * DecodeNormal(a2));
    float2 uv = bary.x * DecodeUV(a0) + bary.y * DecodeUV(a1) + bary.z * DecodeUV(a2);
    float4 vertexColor = bary.x * DecodeColor(a0) + bary.y * DecodeColor(a1) + bary.z * DecodeColor(a2);
    Model model = pc.modelBuffer[instance.modelIndex];

    MaterialProperties material = pc.materialBuffer[instance.materialIndex];
    float4 albedo = material.colorFactor * vertexColor;
    if (material.textureImageIndices.x >= 0) {
        float2 colorUV = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
        albedo *= assetTextures[material.textureImageIndices.x].SampleLevel(assetSamplers[material.textureSamplerIndices.x], colorUV, HIT_SHADING_TEXTURE_LOD);
    }
    float3 emissive = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        emissive *= assetTextures[material.textureImageIndices.w].SampleLevel(assetSamplers[material.textureSamplerIndices.w], emissiveUV, HIT_SHADING_TEXTURE_LOD).rgb;
    }

    GatherHitSurface s;
    s.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    s.diffuseColor = albedo.rgb * (1.0 - material.metalRoughFactors.x);
    s.emissive = emissive;
    return s;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGIGather(uniform GIGatherPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 gatherPixel = dtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    uint2 pixel = GIGatherRepresentativePixel(gatherPixel, pc.frameIndex, pc.renderExtent);

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float2 data = float2(0.0, float(GI_GATHER_TIER_INVALID));

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth > 0.0 && (gt1.w & 1u) != 0u) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        float3 viewNormal = UnpackNormalOct16(gt1.r);
        float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
        float3 worldPosition = mul(scene.invView, float4(viewPosition, 1.0)).xyz;

        if (pc.bSkipRay != 0u) {
            float3 radiance = float3(0.0);
            uint tier;
            WorldCacheKey key = WorldCacheComputeKey(worldPosition, worldN, scene.cameraWorldPos.xyz);
            uint slot = WorldCacheHashFind(pc.cacheEntries, key);
            uint packedRadiance = slot != WORLD_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : WORLD_CACHE_RADIANCE_UNSHADED;
            if (packedRadiance != WORLD_CACHE_RADIANCE_UNSHADED) {
                radiance = UnpackEmissiveRGBE(packedRadiance);
                tier = GI_GATHER_TIER_CACHE;
            } else {
                float3 indirectDiffuse = float3(0.0);
                if (pc.skyboxIndex >= 0) {
                    indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
                }
                if (pc.bCascadesValid != 0) {
                    float3 cameraDirection = normalize(mul((float3x3)scene.invView, viewPosition));
                    float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades);
                    indirectDiffuse = lerp(indirectDiffuse, gi.rgb, gi.w);
                }
                // No hit material to modulate by; the shading pixel's own albedo is applied once downstream (matches the non-gather DDGI path).
                radiance = indirectDiffuse;
                tier = GI_GATHER_TIER_PROBE;
            }

            radiance = min(radiance, float3(GI_GATHER_MAX_RADIANCE));
            shR = float4(radiance.r, 0.0, 0.0, 0.0);
            shG = float4(radiance.g, 0.0, 0.0, 0.0);
            shB = float4(radiance.b, 0.0, 0.0, 0.0);
            data = float2(0.0, float(tier));
        } else {
        uint rng = SeedRng(gatherPixel, pc.frameIndex, 0x61FAu);
        float u1 = HashStep(rng);
        float u2 = HashStep(rng);
        float3 direction = SampleCosineHemisphere(worldN, u1, u2);

        RayDesc ray;
        ray.Origin = worldPosition + worldN * 0.05;
        ray.Direction = direction;
        ray.TMin = 0.0;
        ray.TMax = 10000.0;

        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(rdgAccelerationStructures[pc.tlasIndex], 0, INSTANCE_MASK_DDGI, ray);
        q.Proceed();

        float3 radiance = float3(0.0);
        float hitT = 10000.0;
        uint tier;
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            hitT = q.CommittedRayT();
            if (q.CommittedTriangleFrontFace()) {
                Instance instance = pc.instanceBuffer[q.CommittedInstanceID()];
                float3 hitWorldPos = ray.Origin + direction * hitT;
                GatherHitSurface hit = DecodeGatherHit(pc, instance, q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());

                bool bSkipEmission = pc.bEmissiveIsDI != 0u && instance.emissiveTriLightBase != ~0x0u;

                // Tier 0: last frame's on-screen lit color at the hit, when visible and geometry agrees (mirrors reflection_shade).
                bool bResolved = false;
                if (!bSkipEmission && pc.litHistoryIndex != ~0x0u) {
                    float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
                    if (prevClip.w > 0.0) {
                        float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                        if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                            int2 prevPixel = clamp(int2(prevUV * float2(pc.renderExtent)), int2(0), int2(pc.renderExtent) - int2(1));
                            float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(prevPixel, 0)).r;
                            if (prevRawDepth > 0.0) {
                                float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                                if (abs(prevViewDepth - prevClip.w) <= 0.03 * prevClip.w + 0.05) {
                                    uint4 prevGt1 = rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(prevPixel, 0));
                                    float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(prevGt1.r)));
                                    float prevRoughness;
                                    float prevMetallic;
                                    UnpackPBR(prevGt1.b, prevRoughness, prevMetallic);
                                    // Specular-dominant pixels carry view-dependent radiance (a mirror shows the light source itself); fall through to the diffuse-only tiers.
                                    if (dot(hit.normal, prevWorldN) > 0.5 && prevMetallic < 0.5 && prevRoughness > GI_GATHER_SCREEN_MIN_ROUGHNESS) {
                                        radiance = rdgTexturesFloat4[pc.litHistoryIndex].Load(int3(prevPixel, 0)).rgb;
                                        tier = GI_GATHER_TIER_SCREEN;
                                        bResolved = true;
                                    }
                                }
                            }
                        }
                    }
                }

                if (!bResolved) {
                    uint packedRadiance = WORLD_CACHE_RADIANCE_UNSHADED;
                    if (!bSkipEmission) {
                        WorldCacheKey key = WorldCacheComputeKey(hitWorldPos, hit.normal, scene.cameraWorldPos.xyz);
                        uint slot = WorldCacheHashFind(pc.cacheEntries, key);
                        packedRadiance = slot != WORLD_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : WORLD_CACHE_RADIANCE_UNSHADED;
                    }
                    if (packedRadiance != WORLD_CACHE_RADIANCE_UNSHADED) {
                        radiance = UnpackEmissiveRGBE(packedRadiance);
                        tier = GI_GATHER_TIER_CACHE;
                    } else {
                        // No light loop at fallback hits; their own direct light is missing (TDA's same trade).
                        float3 indirectDiffuse = float3(0.0);
                        if (pc.skyboxIndex >= 0) {
                            indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], hit.normal) * pc.iblIntensity;
                        }
                        if (pc.bCascadesValid != 0) {
                            float4 gi = DDGISampleIrradianceCascaded(hitWorldPos, hit.normal, direction, pc.ddgiCascades);
                            indirectDiffuse = lerp(indirectDiffuse, gi.rgb, gi.w);
                        }
                        radiance = hit.diffuseColor * indirectDiffuse + (bSkipEmission ? float3(0.0) : hit.emissive);
                        tier = GI_GATHER_TIER_PROBE;
                    }
                }
            } else {
                tier = GI_GATHER_TIER_BACKFACE;
            }
        } else {
            if (pc.skyboxIndex >= 0) {
                // Matches the probe trace miss path: Y flip + prefiltered mip 4.
                float3 skyDirection = float3(direction.x, -direction.y, direction.z);
                radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 4).rgb * pc.iblIntensity;
            }
            tier = GI_GATHER_TIER_SKY;
        }

        // Prescaled so eval at n is sh.x + dot(sh.yzw, n) = E/pi; the NoD clamp bounds the 1/pdf weight (eval amplification capped ~1.5x).
        radiance = min(radiance, float3(GI_GATHER_MAX_RADIANCE));
        float NoD = max(dot(worldN, direction), 0.25);
        float l0 = 0.25 / NoD;
        float3 l1 = direction * (0.5 / NoD);
        shR = float4(radiance.r * l0, radiance.r * l1);
        shG = float4(radiance.g * l0, radiance.g * l1);
        shB = float4(radiance.b * l0, radiance.b * l1);
        data = float2(hitT, float(tier));
        }
    }

    rdgStorageFloat4[pc.shRIndex][gatherPixel] = shR;
    rdgStorageFloat4[pc.shGIndex][gatherPixel] = shG;
    rdgStorageFloat4[pc.shBIndex][gatherPixel] = shB;
    rdgStorageFloat2[pc.dataIndex][gatherPixel] = data;
}
