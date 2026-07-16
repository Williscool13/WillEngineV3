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

static float3 SampleCosineHemisphere(float3 n, float u1, float u2)
{
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return normalize(r * cos(phi) * t + r * sin(phi) * b + sqrt(max(1.0 - u1, 0.0)) * n);
}

static float3 GIGatherViewPosAt(GIGatherPushConstant pc, SceneData scene, int2 px)
{
    px = clamp(px, int2(0), int2(pc.renderExtent) - 1);
    float d = rdgTexturesFloat[pc.depthIndex].Load(int3(px, 0)).r;
    float vd = ScreenToViewSpaceDepth(d, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float2 uv = (float2(px) + 0.5) / float2(pc.renderExtent);
    return CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, vd);
}

struct GatherHitSurface
{
    float3 normal;
    float3 diffuseColor;
    bool bEmissive;
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
    GatherHitSurface s;
    s.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    s.diffuseColor = albedo.rgb * (1.0 - material.metalRoughFactors.x);
    s.bEmissive = any(material.emissiveFactor.rgb * material.emissiveFactor.w > float3(0.0));
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
            int2 basePx = int2(pixel);
            float3 vR = GIGatherViewPosAt(pc, scene, basePx + int2(1, 0));
            float3 vL = GIGatherViewPosAt(pc, scene, basePx - int2(1, 0));
            float3 vD = GIGatherViewPosAt(pc, scene, basePx + int2(0, 1));
            float3 vU = GIGatherViewPosAt(pc, scene, basePx - int2(0, 1));
            float3 ddxV = abs(vR.z - viewPosition.z) < abs(vL.z - viewPosition.z) ? (vR - viewPosition) : (viewPosition - vL);
            float3 ddyV = abs(vD.z - viewPosition.z) < abs(vU.z - viewPosition.z) ? (vD - viewPosition) : (viewPosition - vU);
            float3 geoN = normalize(mul((float3x3)scene.invView, normalize(cross(ddxV, ddyV))));
            if (dot(geoN, worldN) < 0.0) { geoN = -geoN; }
            WorldCacheKey key = WorldCacheComputeKey(worldPosition, geoN, scene.cameraWorldPos.xyz);
            uint slot = WorldCacheHashFindLodTolerant(pc.cacheEntries, key, worldPosition);
            uint2 packedRadiance = slot != WORLD_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : uint2(WORLD_CACHE_RADIANCE_UNSHADED, WORLD_CACHE_RADIANCE_UNSHADED);
            uint cellCount = slot != WORLD_CACHE_HASH_INVALID ? (pc.cacheCells[slot].changeStreak >> 16u) & 0xFFu : 0u;
            // Count gate: a count-1 cell is one bary-jittered point; the probe tier is smoother while cells stream in.
            if (WorldCacheRadianceShaded(packedRadiance) && cellCount >= 2u) {
                radiance = WorldCacheUnpackRadiance(packedRadiance);
                tier = GI_GATHER_TIER_CACHE;
                // Reader keeps the cell resident: probe rays alone can let a gather-consumed sparse cell age past the LRU window.
                pc.cacheCells[slot].lastTouched = pc.frameIndex;
            } else {
                float3 indirectDiffuse = float3(0.0);
                if (pc.skyboxIndex >= 0) {
                    indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], worldN) * pc.iblIntensity;
                }
                if (pc.bCascadesValid != 0) {
                    float3 cameraDirection = normalize(mul((float3x3)scene.invView, viewPosition));
                    float4 gi = DDGISampleIrradianceCascaded(worldPosition, worldN, cameraDirection, pc.ddgiCascades);
                    indirectDiffuse = gi.rgb * gi.w + indirectDiffuse * (1.0 - max(gi.w, DDGICascadeSetEdgeFade(worldPosition, pc.ddgiCascades)));
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
        float2 xi = SpatioTemporalBlueNoise2D(gatherPixel, pc.frameIndex);
        float3 direction = SampleCosineHemisphere(worldN, xi.x, xi.y);

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

                // Tier 0: last frame's on-screen lit color at the hit, when visible and geometry agrees (mirrors reflection_shade).
                // Emission never rides the gather (emissive DI belongs to ReSTIR tri-lights, absent otherwise by design; probes carry the bounce), and lit history at an emitter contains its glow.
                bool bResolved = false;
                if (!hit.bEmissive && pc.litHistoryIndex != ~0x0u) {
                    float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
                    if (prevClip.w > 0.0) {
                        float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                        if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                            // Validity-weighted bilinear: every gate applies per corner, so a specular or disoccluded texel contributes nothing instead of bleeding its spike into the blend.
                            float2 texelPos = prevUV * float2(pc.renderExtent) - 0.5;
                            int2 base = int2(floor(texelPos));
                            float2 f = texelPos - float2(base);
                            float3 sum = float3(0.0);
                            float sumW = 0.0;
                            [unroll]
                            for (uint c = 0; c < 4; c++) {
                                int2 offset = int2(int(c & 1u), int((c >> 1u) & 1u));
                                float w = (offset.x != 0 ? f.x : 1.0 - f.x) * (offset.y != 0 ? f.y : 1.0 - f.y);
                                if (w <= 0.0) { continue; }
                                int2 corner = clamp(base + offset, int2(0), int2(pc.renderExtent) - int2(1));
                                float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(corner, 0)).r;
                                if (prevRawDepth <= 0.0) { continue; }
                                float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                                if (abs(prevViewDepth - prevClip.w) > GI_GATHER_DISOCCLUSION_SENSITIVITY * abs(scene.ndcToViewMul.y) * prevClip.w) { continue; }
                                uint4 prevGt1 = rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(corner, 0));
                                float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(prevGt1.r)));
                                float prevRoughness;
                                float prevMetallic;
                                UnpackPBR(prevGt1.b, prevRoughness, prevMetallic);
                                // Specular-dominant pixels carry view-dependent radiance (a mirror shows the light source itself); fall through to the diffuse-only tiers.
                                if (dot(hit.normal, prevWorldN) <= 0.5 || prevMetallic >= 0.5 || prevRoughness <= GI_GATHER_SCREEN_MIN_ROUGHNESS) { continue; }
                                sum += rdgTexturesFloat4[pc.litHistoryIndex].Load(int3(corner, 0)).rgb * w;
                                sumW += w;
                            }
                            if (sumW > 1e-3) {
                                radiance = sum / sumW;
                                tier = GI_GATHER_TIER_SCREEN;
                                bResolved = true;
                            }
                        }
                    }
                }

                if (!bResolved) {
                    WorldCacheKey key = WorldCacheComputeKey(hitWorldPos, hit.normal, scene.cameraWorldPos.xyz);
                    uint slot = WorldCacheHashFindLodTolerant(pc.cacheEntries, key, hitWorldPos);
                    uint2 packedRadiance = slot != WORLD_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : uint2(WORLD_CACHE_RADIANCE_UNSHADED, WORLD_CACHE_RADIANCE_UNSHADED);
                    uint cellCount = slot != WORLD_CACHE_HASH_INVALID ? (pc.cacheCells[slot].changeStreak >> 16u) & 0xFFu : 0u;
                    if (WorldCacheRadianceShaded(packedRadiance) && cellCount >= 2u) {
                        radiance = WorldCacheUnpackRadiance(packedRadiance);
                        tier = GI_GATHER_TIER_CACHE;
                        pc.cacheCells[slot].lastTouched = pc.frameIndex;
                    } else {
                        // No light loop at fallback hits; their own direct light is missing (TDA's same trade).
                        float3 indirectDiffuse = float3(0.0);
                        if (pc.skyboxIndex >= 0) {
                            indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], hit.normal) * pc.iblIntensity;
                        }
                        if (pc.bCascadesValid != 0) {
                            float4 gi = DDGISampleIrradianceCascaded(hitWorldPos, hit.normal, direction, pc.ddgiCascades);
                            indirectDiffuse = gi.rgb * gi.w + indirectDiffuse * (1.0 - max(gi.w, DDGICascadeSetEdgeFade(hitWorldPos, pc.ddgiCascades)));
                        }
                        radiance = hit.diffuseColor * indirectDiffuse;
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
