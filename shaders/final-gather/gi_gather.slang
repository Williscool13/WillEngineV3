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

// =============== Shader Summary ============
// One cosine-weighted ray per half-res pixel, zero shading at the hit: cache radiance, else albedo * DDGI irradiance (one material fetch), skybox on miss. MC-projected into per-channel 2-band SH, evaluated at the full-res normal at composite.

static const uint GI_GATHER_TIER_CACHE = 0u;
static const uint GI_GATHER_TIER_PROBE = 1u;
static const uint GI_GATHER_TIER_SKY = 2u;
static const uint GI_GATHER_TIER_BACKFACE = 3u;
static const uint GI_GATHER_TIER_INVALID = 4u;

static const uint2 GI_GATHER_QUAD_OFFSETS[4] = { uint2(0, 0), uint2(1, 1), uint2(1, 0), uint2(0, 1) };

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

    // Cycle the representative pixel through the quad so temporal filtering sees all four.
    uint2 pixel = min(gatherPixel * 2u + GI_GATHER_QUAD_OFFSETS[pc.frameIndex & 3u], pc.renderExtent - 1u);

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

                WorldCacheKey key = WorldCacheComputeKey(hitWorldPos, hit.normal, scene.cameraWorldPos.xyz);
                uint slot = WorldCacheHashFind(pc.cacheEntries, key);
                uint packedRadiance = slot != WORLD_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : WORLD_CACHE_RADIANCE_UNSHADED;
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
                    radiance = hit.diffuseColor * indirectDiffuse + hit.emissive;
                    tier = GI_GATHER_TIER_PROBE;
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

        // Prescaled so eval at n is sh.x + dot(sh.yzw, n) = E/pi; the NoD clamp bounds the 1/pdf weight.
        float NoD = max(dot(worldN, direction), 0.05);
        float l0 = 0.25 / NoD;
        float3 l1 = direction * (0.5 / NoD);
        shR = float4(radiance.r * l0, radiance.r * l1);
        shG = float4(radiance.g * l0, radiance.g * l1);
        shB = float4(radiance.b * l0, radiance.b * l1);
        data = float2(hitT, float(tier));
    }

    rdgStorageFloat4[pc.shRIndex][gatherPixel] = shR;
    rdgStorageFloat4[pc.shGIndex][gatherPixel] = shG;
    rdgStorageFloat4[pc.shBIndex][gatherPixel] = shB;
    rdgStorageFloat2[pc.dataIndex][gatherPixel] = data;
}
