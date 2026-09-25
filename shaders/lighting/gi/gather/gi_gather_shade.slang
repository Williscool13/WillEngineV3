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
import shader_constants;
import random_common;
import hit_shading_common;
import radiance_cache_hash_common;
import radiance_cache_interop;
import gi_gather_common;
import reflection_probe_interop;
import reflection_probe_common;

// Exposed units, relative to display white.
static const float GI_GATHER_MAX_EXPOSED_RADIANCE = 40.0;

/** Hue-preserving; must match radiance_cache_shade so every tier tints the same. */
static float3 GIGatherClampRadiance(float3 radiance, float maxRadiance)
{
    if (any(isnan(radiance))) {
        return float3(0.0);
    }
    float maxChannel = max(radiance.r, max(radiance.g, radiance.b));
    return maxChannel > maxRadiance ? radiance * (maxRadiance / maxChannel) : radiance;
}

struct GatherHitSurface {
    float3 normal;
    float3 diffuseColor;
    float roughness;
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
        albedo *= assetTextures[NonUniformResourceIndex(material.textureImageIndices.x)].SampleLevel(assetSamplers[NonUniformResourceIndex(material.textureSamplerIndices.x)], colorUV, HIT_SHADING_TEXTURE_LOD);
    }
    GatherHitSurface s;
    s.normal = normalize(mul(Adjugate((float3x3)model.modelMatrix), normalOS));
    float metallic;
    float mrRoughness;
    SampleMetalRoughLod(material, uv, HIT_SHADING_TEXTURE_LOD, metallic, mrRoughness);
    s.diffuseColor = albedo.rgb * (1.0 - metallic);
    s.roughness = max(mrRoughness, MIN_ROUGHNESS);
    s.bEmissive = any(material.emissiveFactor.rgb * material.emissiveFactor.w > float3(0.0));
    return s;
}

/** Scaled by bounceIntensity to match what ShadeRadianceCacheCell feeds the cache. */
static float3 ResolveWorldIndirect(GIGatherPushConstant pc, float3 hitWorldPos, float3 normal, float3 direction, float hitT, ReflectionProbeBlend probeBlend)
{
    float3 indirectDiffuse = float3(0.0);
    float giW = 0.0;
    if (pc.skyboxIndex >= 0) {
        indirectDiffuse = DiffuseIrradiance(assetCubemaps[pc.skyboxIndex], assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], normal) * pc.iblIntensity;
    }
    if (pc.bCascadesValid != 0) {
        float edgeFade;
        float4 gi = DDGISampleIrradianceCascaded(hitWorldPos, normal, direction, pc.ddgiCascades, 0.75 * hitT, edgeFade);
        indirectDiffuse = gi.rgb * pc.bounceIntensity * gi.w + indirectDiffuse * (1.0 - max(gi.w, edgeFade));
        giW = gi.w;
    }
    if (probeBlend.indexA >= 0) {
        float3 probeIrr = SampleReflectionProbeIrradianceBlended(probeBlend, pc.reflectionProbes, normal);
        // Fill, not override: a cubemap has no positional validity.
        indirectDiffuse = lerp(indirectDiffuse, probeIrr, probeBlend.coverage * (1.0 - giW));
    }
    return indirectDiffuse;
}

/** Reader keep-alive: carry forward stamps touched cells later, once no graphics work can still read the table it rebuilds. */
static void GatherTouchCell(GIGatherPushConstant pc, uint slot)
{
    if (pc.touchEntries == nullptr) {
        return;
    }
    bool isNew;
    RadianceCacheHashInsert(pc.touchEntries, nullptr, nullptr, RadianceCacheUnpackKey(pc.cacheKeys[slot]), isNew);
}

static float3 ResolveHitFallback(GIGatherPushConstant pc, float3 cameraWorldPos, float3 hitWorldPos, GatherHitSurface hit, float3 direction, float hitT, out uint tier)
{
    float3 radiance;
    RadianceCacheKey key = RadianceCacheComputeKey(hitWorldPos, hit.normal, cameraWorldPos);
    uint slot = RadianceCacheHashFindLodTolerant(pc.cacheEntries, pc.cacheKeys, key, hitWorldPos, hit.normal);
    uint2 packedRadiance = slot != RADIANCE_CACHE_HASH_INVALID ? pc.cacheCells[slot].packedRadiance : uint2(RADIANCE_CACHE_RADIANCE_UNSHADED, RADIANCE_CACHE_RADIANCE_UNSHADED);
    if (RadianceCacheRadianceShaded(packedRadiance)) {
        radiance = RadianceCacheUnpackRadiance(packedRadiance) * hit.diffuseColor;
        tier = GI_GATHER_TIER_CACHE;
        GatherTouchCell(pc, slot);
    } else {
        ReflectionProbeBlend probeBlend = ReflectionProbeBlendNone();
        if (pc.reflectionProbeCount > 0u) {
            probeBlend = PickReflectionProbeBinned(hitWorldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, cameraWorldPos);
        }
        // Deliberately no light loop at fallback hits. V = N, as the cache cell it stands in for.
        radiance = hit.diffuseColor * OrenNayarDirectionalAlbedo(1.0, hit.roughness) * ResolveWorldIndirect(pc, hitWorldPos, hit.normal, direction, hitT, probeBlend);
        tier = GI_GATHER_TIER_PROBE;
        if (probeBlend.indexA >= 0 && probeBlend.coverage > 0.5) {
            tier = GI_GATHER_TIER_BAKED;
        }
    }
    return radiance;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGIGatherShade(uniform GIGatherPushConstant pc, uint3 dtid: SV_DispatchThreadID)
{
    uint2 gatherPixel = dtid.xy;
    if (any(gatherPixel >= pc.gatherExtent)) {
        return;
    }

    uint2 pixel = GIGatherRepresentativePixel(gatherPixel, pc.renderExtent, pc.gatherScale);

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float3 mean = float3(0.0);
    float2 data = float2(0.0, float(GI_GATHER_TIER_INVALID));
    uint2 guide = uint2(0u, 0u);
    float skyVis = 0.0;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth > 0.0 && (gt1.w & 1u) != 0u) {
        SceneData scene = pc.sceneData[pc.sceneDataIndex];
        float3 viewNormal = UnpackNormalOct16(gt1.r);
        float3 worldN = normalize(mul((float3x3)scene.invView, viewNormal));

        float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
        float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        guide = uint2(PackNormalOct16(GIGatherGuideNormal(scene, pc.depthIndex, int2(pixel), pc.renderExtent, viewNormal)), asuint(viewSpaceDepth));
        float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
        float3 worldPosition = mul(scene.invView, float4(viewPosition, 1.0)).xyz;

        uint rayCount = max(pc.rayCount, 1u);
        bool bScreenDiffuse = pc.screenDiffuseHistoryIndex != ~0x0u;
        uint hitBase = (gatherPixel.y * pc.gatherExtent.x + gatherPixel.x) * rayCount;
        float4 sumR = float4(0.0);
        float4 sumG = float4(0.0);
        float4 sumB = float4(0.0);
        float3 sumMean = float3(0.0);
        uint validCount = 0u;
        float skyVisSum = 0.0;
        float firstHitT = 0.0;
        uint firstTier = GI_GATHER_TIER_BACKFACE;
        float lastHitT = 0.0;
        uint lastTier = GI_GATHER_TIER_BACKFACE;
        float3 toCamera = normalize(scene.cameraWorldPos.xyz - worldPosition);
        float3 rayOrigin = GIGatherRayOrigin(worldPosition, worldN, toCamera, RayOriginPixelFootprint(viewSpaceDepth, scene.ndcToViewMulXPixelSize, 1.0));
        for (uint sampleIndex = 0u; sampleIndex < rayCount; ++sampleIndex) {
            GIGatherHit rec = pc.hitBuffer[hitBase + sampleIndex];
            float3 direction = rec.direction;

            float3 radiance = float3(0.0);
            float hitT = 10000.0;
            uint tier;
            if (rec.hitT != GI_GATHER_HIT_SKY) {
                hitT = rec.hitT;
                Instance instance = pc.instanceBuffer[rec.instanceID];
                float3 hitWorldPos = rayOrigin + direction * hitT;
                GatherHitSurface hit = DecodeGatherHit(pc, instance, rec.primIndex & ~GI_GATHER_HIT_FRONT_FACE_BIT, rec.bary);
                bool bFrontFace = (rec.primIndex & GI_GATHER_HIT_FRONT_FACE_BIT) != 0u;
                if (IsTwoSidedBackHit(pc.materialBuffer, instance, bFrontFace)) {
                    hit.normal = -hit.normal;
                    bFrontFace = true;
                }

                // Emitters skipped: emission never rides the gather, and lit history at an emitter contains its glow.
                bool bResolved = false;
                if (bFrontFace && !hit.bEmissive && pc.litHistoryIndex != ~0x0u) {
                    float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
                    if (prevClip.w > 0.0) {
                        float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                        if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                            float2 texelPos = prevUV * float2(pc.renderExtent) - 0.5;
                            int2 base = int2(floor(texelPos));
                            float2 f = texelPos - float2(base);
                            float3 sum = float3(0.0);
                            float sumW = 0.0;
                            float4 prevRawDepths = rdgTexturesFloat[pc.depthHistoryIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], float2(base + 1) / float2(pc.renderExtent)).wzxy;
                            [unroll]
                            for (uint c = 0; c < 4; c++) {
                                int2 offset = int2(int(c & 1u), int((c >> 1u) & 1u));
                                float w = (offset.x != 0 ? f.x : 1.0 - f.x) * (offset.y != 0 ? f.y : 1.0 - f.y);
                                if (w <= 0.0) {
                                    continue;
                                }
                                int2 corner = clamp(base + offset, int2(0), int2(pc.renderExtent) - int2(1));
                                float prevRawDepth = prevRawDepths[c];
                                if (prevRawDepth <= 0.0) {
                                    continue;
                                }
                                float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                                if (abs(prevViewDepth - prevClip.w) > GI_GATHER_DISOCCLUSION_SENSITIVITY * abs(scene.ndcToViewMul.y) * prevClip.w) {
                                    continue;
                                }
                                uint4 prevGt1 = rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(corner, 0));
                                float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(prevGt1.r)));
                                if (dot(hit.normal, prevWorldN) <= 0.5) {
                                    continue;
                                }
                                // Rays through a light proxy hit the surface behind it and would reproject onto the emitter's glow.
                                if ((prevGt1.w & 2u) != 0u) {
                                    continue;
                                }
                                sum += rdgTexturesFloat4[bScreenDiffuse ? pc.screenDiffuseHistoryIndex : pc.litHistoryIndex].Load(int3(corner, 0)).rgb * w;
                                sumW += w;
                            }
                            if (sumW > 1e-3) {
                                radiance = sum / (sumW * scene.prevPreExposure);
                                if (bScreenDiffuse) {
                                    radiance *= hit.diffuseColor * OrenNayarDirectionalAlbedo(1.0, hit.roughness);
                                }
                                tier = GI_GATHER_TIER_SCREEN;
                                bResolved = true;
                            }
                        }
                    }
                }

                if (!bFrontFace) {
                    // Near backface = origin buried in geometry; far backface = open space behind a single-sided surface.
                    if (hitT > GI_GATHER_BACKFACE_GUARD_DISTANCE) {
                        hit.normal = -hit.normal;
                        radiance = ResolveHitFallback(pc, scene.cameraWorldPos.xyz, hitWorldPos, hit, direction, hitT, tier);
                    } else {
                        tier = GI_GATHER_TIER_BACKFACE;
                    }
                } else if (!bResolved) {
                    radiance = ResolveHitFallback(pc, scene.cameraWorldPos.xyz, hitWorldPos, hit, direction, hitT, tier);
                }
            } else {
                skyVisSum += 1.0;
                tier = GI_GATHER_TIER_SKY;
            }

            // The SH carries only the tilt; the plain mean is the unbiased E/pi at worldN.
            radiance = GIGatherClampRadiance(radiance, min(pc.maxRayRadiance > 0.0 ? pc.maxRayRadiance : 1e30, GI_GATHER_MAX_EXPOSED_RADIANCE / scene.preExposure));
            float NoD = max(dot(worldN, direction), 0.25);
            float l0 = 0.25 / NoD;
            float3 l1 = direction * (0.5 / NoD);
            lastHitT = hitT;
            lastTier = tier;
            if (tier != GI_GATHER_TIER_BACKFACE) {
                if (validCount == 0u) {
                    firstHitT = hitT;
                    firstTier = tier;
                }
                sumR += float4(radiance.r * l0, radiance.r * l1);
                sumG += float4(radiance.g * l0, radiance.g * l1);
                sumB += float4(radiance.b * l0, radiance.b * l1);
                sumMean += radiance;
                validCount += 1u;
            }
        }

        if (validCount > 0u) {
            float invCount = scene.preExposure / float(validCount);
            shR = sumR * invCount;
            shG = sumG * invCount;
            shB = sumB * invCount;
            mean = sumMean * invCount;
            data = float2(firstHitT, float(firstTier));
        } else {
            data = float2(lastHitT, float(lastTier));
        }
        skyVis = skyVisSum / float(rayCount);
    }

    rdgStorageFloat4[pc.shRIndex][gatherPixel] = shR;
    rdgStorageFloat4[pc.shGIndex][gatherPixel] = shG;
    rdgStorageFloat4[pc.shBIndex][gatherPixel] = shB;
    rdgStorageFloat2[pc.dataIndex][gatherPixel] = data;
    rdgStorageUInt2[pc.guideOutIndex][gatherPixel] = guide;
    rdgStorageFloat4[pc.skyVisIndex][gatherPixel] = float4(mean, skyVis);
}
