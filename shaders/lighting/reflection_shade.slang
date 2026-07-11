import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import reflection_interop;
import ddgi_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;
import restir_common;
import ddgi_common;
import hit_shading_common;
import random_common;
import binning_common;

import bindless_declarations;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeReflectionShade(uniform ReflectionShadePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    uint field = pc.activeCheckerboardField;
    if (field != 0u && !IsActiveCheckerboardPixel(int2(pixel), false, field)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, ~0x0, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        return;
    }

    float roughness = max(li.roughness, 0.045);
    if (roughness > pc.roughnessMax) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);
    float3 worldN = normalize(mul(float3x3(scene.invView), N));
    float3 worldPos = mul(scene.invView, float4(viewPosition, 1.0)).xyz;
    // Must match the base pass's BRDF ray origin exactly: the descriptor's hitT was measured from this same offset.
    float3 rayOrigin = worldPos + worldN * 0.05;

    uint2 rcoord = PixelPosToReservoirPos(pixel, field);
    uint pixelIdx = ReservoirIndex(rcoord, pc.renderExtent.x, field);
    ReflectionHitDescriptor hit = pc.reflectionDescriptors[pixelIdx];

    float3 radiance = float3(0.0);
    float hitTOut = 0.0;
    if (hit.hitT >= 0.0) {
        Instance inst = pc.instanceBuffer[hit.instanceID];
        uint rng = SeedRng(pixel, pc.frameIndex, 0xA17Fu);

        DDGIProbeTracePushConstant ddgiPC = (DDGIProbeTracePushConstant)0;
        ddgiPC.lightData = pc.lightData;
        ddgiPC.instanceBuffer = pc.instanceBuffer;
        ddgiPC.primitiveBuffer = pc.primitiveBuffer;
        ddgiPC.modelBuffer = pc.modelBuffer;
        ddgiPC.materialBuffer = pc.materialBuffer;
        ddgiPC.indexBuffer = pc.indexBuffer;
        ddgiPC.vertexAttrBuffer = pc.vertexAttrBuffer;
        ddgiPC.tlasIndex = pc.tlasIndex;
        ddgiPC.previousCascades = pc.ddgiCascades;
        ddgiPC.bFeedbackValid = pc.bDDGIApply;

        WorldGridPunctualLights worldGridLights = (WorldGridPunctualLights)0;
        if (pc.worldGridBuffer != nullptr) {
            worldGridLights.view = scene.view;
            worldGridLights.camPos = scene.cameraWorldPos.xyz;
            worldGridLights.grid = pc.worldGridBuffer;
            worldGridLights.indexList = pc.worldGridIndexList;
        }

        float3 hitWorldPos = rayOrigin + hit.hitT * hit.rayDir;

        // Prefer last frame's lit color at the hit (fully shadowed lighting); analytic fallback shades local lights with no visibility.
        bool bShaded = false;
        if (pc.litHistoryIndex != ~0u) {
            float4 prevClip = mul(scene.prevViewProj, float4(hitWorldPos, 1.0));
            if (prevClip.w > 0.0) {
                float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
                if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                    int2 prevPixel = clamp(int2(prevUV * float2(pc.renderExtent)), int2(0, 0), int2(pc.renderExtent) - int2(1, 1));
                    float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(prevPixel, 0)).r;
                    if (prevRawDepth > 0.0) {
                        float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                        if (abs(prevViewDepth - prevClip.w) <= 0.03 * prevClip.w + 0.05) {
                            float3 hitN = DecodeHitNormal(ddgiPC, inst, hit.primIndex, hit.bary);
                            float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(prevPixel, 0)).r)));
                            if (dot(hitN, prevWorldN) > 0.5) {
                                radiance = rdgTexturesFloat4[pc.litHistoryIndex].Load(int3(prevPixel, 0)).rgb;
                                bShaded = true;
                            }
                        }
                    }
                }
            }
        }
        if (!bShaded) {
            radiance = ShadeProbeRayHitAnalytical(ddgiPC, inst, hit.primIndex, hit.bary, hitWorldPos, hit.rayDir, rng, worldGridLights);
        }
        hitTOut = hit.hitT;
    } else if (hit.hitT == REFLECTION_HIT_T_SKY && pc.skyboxIndex >= 0) {
        // Matches the Y flip the visible skybox pass applies when sampling the cubemap.
        float3 skyDirection = float3(hit.rayDir.x, -hit.rayDir.y, hit.rayDir.z);
        radiance = assetCubemaps[pc.skyboxIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], skyDirection, 0).rgb;
        hitTOut = 10000.0;
    }
    // hit.hitT == REFLECTION_HIT_T_OWNED (or the NaN clear-sentinel): radiance and hitTOut stay 0, matching the "no hit" convention RELAX's temporal accumulation expects.

    radiance *= pc.intensity;

    // Demodulate by the mirror surface's specular reflectance so the denoiser sees a near-albedo-free signal.
    float nDotV = max(dot(N, V), 0.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
    float3 specReflectance = max(F0 * envBRDF.x + envBRDF.y, float3(0.01, 0.01, 0.01));
    float3 demodulated = radiance / specReflectance;

    // Packed (rcoord) when a checkerboard-aware consumer (the reflection RELAX prepass) will resolve it; otherwise dense-duplicated to both fields so a direct .Load(pixel) fallback (denoiser off, or ReBLUR/plain modes) sees no holes. Matches lighting_pbr_restir.slang's writeCoord logic exactly.
    float4 outColor = float4(demodulated, hitTOut);
    uint2 writeCoord = (field != 0u && pc.bCheckerboardPacked != 0u) ? rcoord : pixel;
    rdgStorageFloat4[pc.outputIndex][writeCoord] = outColor;
    if (field != 0u && pc.bCheckerboardPacked == 0u) {
        int2 o = OtherFieldPixel(pixel, field);
        if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
            rdgStorageFloat4[pc.outputIndex][uint2(o)] = outColor;
        }
    }
}
