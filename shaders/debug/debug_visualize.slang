import push_constant_interop;
import constants_interop;
import restir_interop;
import lights_interop;

import bindless_declarations;
import shader_functions;
import denoiser_common;
import restir_common;
import reflection_probe_common;
import regir_hash_common;
import packing_functions;

[[vk::push_constant]]
DebugVisualizePushConstant pc;

static float3 HueToRgb(float hue)
{
    float3 k = frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0));
    return saturate(abs(k * 6.0 - 3.0) - 1.0);
}

// Analytic lights get a hue each. Emissive triangles share their group's hue so a mesh reads as one light, with a brightness step per triangle so its faces stay separable.
/** Emissive triangle -> its group's first light so triangles of one mesh vote together; analytic and invalid pass through. */
static uint CollapseLightIdxToGroup(uint lightIdx)
{
    if (lightIdx == ~0u || lightIdx < uint(MAX_ANALYTIC_LIGHTS) || pc.lightData == nullptr) { return lightIdx; }
    int groupCount = pc.lightData->emissiveGroupCount;
    for (int g = 0; g < groupCount; g++) {
        EmissiveGroup group = pc.lightData->emissiveGroups[g];
        if (lightIdx >= group.firstLight && lightIdx < group.firstLight + group.lightCount) { return group.firstLight; }
    }
    return lightIdx;
}

static float3 LightIdxColor(uint lightIdx)
{
    if (lightIdx == ~0u) { return float3(0.0); }
    const float GOLDEN = 0.6180339887;
    if (lightIdx < uint(MAX_ANALYTIC_LIGHTS) || pc.lightData == nullptr) {
        return HueToRgb(frac(float(lightIdx) * GOLDEN));
    }
    int groupCount = pc.lightData->emissiveGroupCount;
    for (int g = 0; g < groupCount; g++) {
        EmissiveGroup group = pc.lightData->emissiveGroups[g];
        if (lightIdx >= group.firstLight && lightIdx < group.firstLight + group.lightCount) {
            float3 hueColor = HueToRgb(frac(0.35 + float(g) * GOLDEN));
            float triStep = 0.45 + 0.55 * frac(float(lightIdx - group.firstLight) * GOLDEN);
            return hueColor * triStep;
        }
    }
    return float3(0.5);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeDebugVisualize(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.dstExtent.x || threadID.y >= pc.dstExtent.y) {
        return;
    }

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.dstExtent);

    int3 sampleTexCoord = int3(threadID.xy, 0);
    int2 texCoord = uint2(threadID.xy);

    bool bPixelInRenderBounds = threadID.x < uint(pc.srcExtent.x) && threadID.y < uint(pc.srcExtent.y);
    uint2 pixelXY = threadID.xy;

    int2 srcMax = pc.srcExtent - 1;
    uint2 currentActivePixel = uint2(clamp(ActivateCheckerboardPixel(int2(pixelXY), false, pc.checkerboardField), int2(0), srcMax));
    uint2 currentReservoirPos = PixelPosToReservoirPos(currentActivePixel, pc.checkerboardField);
    uint currentStride = RestirCheckerboardStride(uint(pc.srcExtent.x), pc.checkerboardField);
    uint reservoirPixelIdx = currentReservoirPos.y * currentStride + currentReservoirPos.x;
    bool bInReservoirBounds = bPixelInRenderBounds;

    bool bHistoryReservoir = bPixelInRenderBounds;
    uint2 historyActivePixel = uint2(clamp(ActivateCheckerboardPixel(int2(pixelXY), false, pc.historyCheckerboardField), int2(0), srcMax));
    uint2 historyReservoirPos = PixelPosToReservoirPos(historyActivePixel, pc.historyCheckerboardField);
    uint historyStride = RestirCheckerboardStride(uint(pc.srcExtent.x), pc.historyCheckerboardField);
    uint historyReservoirPixelIdx = historyReservoirPos.y * historyStride + historyReservoirPos.x;

    float4 srcPixel = float4(1.0f, 0.0f, 0.0f, 1.0f);
    switch (pc.textureArrayIndex) {
        case 0: // Sampled float4
            srcPixel.xyzw = rdgTexturesFloat4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, 0).rgba;
            break;
        case 1: // Sampled float2
            srcPixel.xy = rdgTexturesFloat2[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, 0).rg;
            break;
        case 2: // Sampled float
            float c = rdgTexturesFloat[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, 0).r;
            srcPixel.xyz = float3(c);
            break;
        case 3: // Sampled uint4
            uint4 v = rdgTexturesUInt4[pc.textureIndexInArray].Load(sampleTexCoord).rgba;
            srcPixel = asfloat(v);
            break;
        case 4: // Sampled uint2
            uint2 b = rdgTexturesUInt2[pc.textureIndexInArray].Load(sampleTexCoord).rg;
            srcPixel.xy = asfloat(b);
            break;
        case 5: // Sampled uint
            srcPixel.x = float(rdgTexturesUInt[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, 0).r);
            break;
    }

    switch (pc.valueTransformationType) {
        case 1: // DepthRemap
        {
            float v = ScreenToViewSpaceDepth(srcPixel.x, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd) / 500.0f;
            srcPixel.xyz = float3(v);
        }
            break;
        case 2: // MultiplyBy1000
            srcPixel.xyz *= 1000.0f;
            break;
        case 3: // MultiplyBy10000
            srcPixel.xyz *= 10000.0f;
            break;
        case 4: // DivideBy1000
            srcPixel.xyz /= 1000.0f;
            break;
        case 5: // DivideBy10000
            srcPixel.xyz /= 10000.0f;
            break;
        case 6: // StencilRemap
            srcPixel.xyz /= 255.0f;
            break;
        case 7: { // VisBuffTriangle - bits [0:7] of vis.x
            uint2 intSrcPixel = asuint(srcPixel.xy);
            if (intSrcPixel.x == 0xFFFFFFFFu && intSrcPixel.y == 0xFFFFFFFFu) { break; }
            uint triIdx = intSrcPixel.x & 0xFF;
            uint h = (triIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 8: { // VisBuffMeshlet - bits [8:31] of vis.x
            uint2 intSrcPixel = asuint(srcPixel.xy);
            if (intSrcPixel.x == 0xFFFFFFFFu && intSrcPixel.y == 0xFFFFFFFFu) { break; }
            uint meshletIdx = intSrcPixel.x >> 8;
            uint h = (meshletIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 9: { // VisBuffInstance - vis.y
            uint2 intSrcPixel = asuint(srcPixel.xy);
            if (intSrcPixel.x == 0xFFFFFFFFu && intSrcPixel.y == 0xFFFFFFFFu) { break; }
            uint instIdx = intSrcPixel.y;
            uint h = (instIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 10: { // GBufferNormal, oct16 packed in gbuffer_one.r
            float3 normal = UnpackNormalOct16(asuint(srcPixel.x));
            srcPixel = float4(normal * 0.5 + 0.5, 1.0);
            break;
        }
        case 11: { // GBufferMotionVectors, R16G16 half in gbuffer_one.g
            float2 mv = UnpackMotionVectors(asuint(srcPixel.y));
            srcPixel = float4(abs(mv) * 50, 0.0, 1.0);
            break;
        }
        case 12: { // GBufferPBR, roughness/metallic in gbuffer_one.b; r=roughness g=metallic
            float roughness, metallic;
            UnpackPBR(asuint(srcPixel.z), roughness, metallic);
            srcPixel = float4(roughness, metallic, 0.0, 1.0);
            break;
        }
        case 13: { // GBufferAlbedo, RGBA8 packed in gbuffer_two.r
            srcPixel = UnpackAlbedoRGBA8(asuint(srcPixel.x));
            break;
        }
        case 14: { // GBufferEmissive, RGBE packed in gbuffer_two.g
            float3 emissive = UnpackEmissiveRGBE(asuint(srcPixel.y));
            srcPixel = float4(emissive, 1.0);
            break;
        }
        case 15: { // VisBuffShadingBucket - shadingBucketIndex from material of instance in vis.y
            uint2 intSrcPixel = asuint(srcPixel.xy);
            if (intSrcPixel.x == 0xFFFFFFFFu && intSrcPixel.y == 0xFFFFFFFFu) { break; }
            uint instIdx = intSrcPixel.y;
            uint matIdx = pc.instanceBuffer[instIdx].materialIndex;
            uint bucketIdx = pc.materialBuffer[matIdx].shadingBucketIndex;
            uint h = (bucketIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 16: { // VisBuffLightingBucket - lightingBucketIndex from material of instance in vis.y
            uint2 intSrcPixel = asuint(srcPixel.xy);
            if (intSrcPixel.x == 0xFFFFFFFFu && intSrcPixel.y == 0xFFFFFFFFu) { break; }
            uint instIdx = intSrcPixel.y;
            uint matIdx = pc.instanceBuffer[instIdx].materialIndex;
            uint bucketIdx = pc.materialBuffer[matIdx].lightingBucketIndex;
            uint h = (bucketIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 17: { // ReservoirLightIdx - hash-color the selected light index from the reservoir buffer
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            srcPixel.xyz = LightIdxColor(pc.reservoirBuffer[reservoirPixelIdx].lightIdx);
            break;
        }
        case 18: { // ReservoirTemporalLightIdx - hash-color the selected light index from the temporal reservoir buffer
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            srcPixel.xyz = LightIdxColor(pc.reservoirTemporalBuffer[reservoirPixelIdx].lightIdx);
            break;
        }
        case 19: { // ReservoirTemporalW - visualize W * 65536 with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirTemporalBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W * 65536.0 < 100.0) { float t = r.W * 65536.0 / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W * 65536.0 < 500.0) { float t = (r.W * 65536.0 - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W * 65536.0 - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 20: { // ReservoirGenerateW - visualize W * 65536 with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W * 65536.0 < 100.0) { float t = r.W * 65536.0 / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W * 65536.0 < 500.0) { float t = (r.W * 65536.0 - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W * 65536.0 - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 21: { // ReservoirSpatialLightIdx - hash-color the selected light index from the spatial reservoir buffer
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            srcPixel.xyz = LightIdxColor(pc.reservoirSpatialBuffer[reservoirPixelIdx].lightIdx);
            break;
        }
        case 22: { // ReservoirSpatialW - visualize W * 65536 with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirSpatialBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W * 65536.0 < 100.0) { float t = r.W * 65536.0 / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W * 65536.0 < 500.0) { float t = (r.W * 65536.0 - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W * 65536.0 - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 23: { // ReservoirHistoryLightIdx - hash-color the selected light index from the history reservoir buffer
            if (!bHistoryReservoir) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            srcPixel.xyz = LightIdxColor(pc.reservoirHistoryBuffer[historyReservoirPixelIdx].lightIdx);
            break;
        }
        case 24: { // ReservoirHistoryW - visualize W * 65536 with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bHistoryReservoir) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirHistoryBuffer[historyReservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W * 65536.0 < 100.0) { float t = r.W * 65536.0 / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W * 65536.0 < 500.0) { float t = (r.W * 65536.0 - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W * 65536.0 - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 25: { // ViewSpacePosition
            float3 vsPos = ReconstructViewPosition(pc.sceneData->invProj, uv, srcPixel.x);
            srcPixel.xyz = abs(vsPos) / 100.0;
            break;
        }
        case 27: { // GBufferViewZDelta, float16 in gbuffer_one.w bits 16-31; abs * 10 for visibility
            float delta = UnpackViewZDelta(asuint(srcPixel.w));
            srcPixel = float4(abs(delta) * 10.0, delta > 0.0 ? abs(delta) * 10.0 : 0.0, delta < 0.0 ? abs(delta) * 10.0 : 0.0, 1.0);
            break;
        }
        case 26: { // NDotV
            float3 N = normalize(UnpackNormalOct16(asuint(srcPixel.x)));
            float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 V = normalize(-viewPos);
            float ndotv = max(dot(N, V), 0.0);
            srcPixel.xyz = ndotv <= 0.0 ? float3(1.0, 0, 0) : float3(0, ndotv, 0);
            break;
        }
        case 28: { // SunShadowVisibility - .r is sqrt(visibility) (binary trace squares to itself); show linear (white lit, black shadowed)
            float v = srcPixel.x * srcPixel.x;
            srcPixel.xyz = float3(v);
            break;
        }
        case 29: { // SunShadowPenumbra - .g packed penumbra (world radius); dark blue where lit (FP16_MAX sentinel), grey ramp otherwise
            float penum = srcPixel.y;
            bool lit = penum >= 60000.0;
            srcPixel.xyz = lit ? float3(0.0, 0.0, 0.15) : float3(saturate(penum * 2.0));
            break;
        }
        case 30: { // SunShadowTiles - sigma_tiles (needsFilter, maxRadius/16, isSky): red = penumbra tile (brighter = bigger radius), grey = uniform, blue = sky
            float needsFilter = srcPixel.x;
            float radius = srcPixel.y;
            float isSky = srcPixel.z;
            if (isSky > 0.5) { srcPixel.xyz = float3(0.0, 0.0, 0.3); }
            else if (needsFilter > 0.5) { srcPixel.xyz = float3(0.3 + 0.7 * radius, 0.05, 0.05); }
            else { srcPixel.xyz = float3(0.12); }
            break;
        }
        case 31: { // SunShadowTileValue - sigma_tiles_smoothed.y dilated filter mask (the tileValue the blur uses to scale its kernel)
            srcPixel.xyz = float3(srcPixel.y);
            break;
        }
        case 32: { // YCoCgSignal
            srcPixel.xyz = max(MLYCoCgToRgb(srcPixel.xyz), 0.0);
            break;
        }
        case 33: { // ReblurInternalData: r = diff accum frames/63, g = spec/63 (raw uint, float() is lossy)
            uint p = rdgTexturesUInt[pc.textureIndexInArray].Load(sampleTexCoord).r;
            srcPixel.xyz = float3(float(p & 0x3Fu), float((p >> 6) & 0x3Fu), 0.0) / 63.0;
            break;
        }
        case 34: { // ReblurData2: r = smb taps/4, g = vmb taps/4, b = virtualHistoryAmount
            uint p = rdgTexturesUInt[pc.textureIndexInArray].Load(sampleTexCoord).r;
            srcPixel.xyz = float3(float(countbits(p & 0xFu)) / 4.0, float(countbits((p >> 4) & 0xFu)) / 4.0, float((p >> 8) & 0x7Fu) / 127.0);
            break;
        }
        case 35: { // BentNormal: oct16 in R32_UINT (raw uint, float() is lossy). Same view-space RGB encoding as case 10 so it A/Bs against GBuffer Normal.
            uint p = rdgTexturesUInt[pc.textureIndexInArray].Load(sampleTexCoord).r;
            srcPixel = float4(UnpackNormalOct16(p) * 0.5 + 0.5, 1.0);
            break;
        }
        case 40: { // ReflectionProbeIndex: hash-color the probe covering the pixel. Black = sky, magenta = no probe.
            float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            if (depth <= 0.0) { srcPixel.xyz = float3(0.0); break; }
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 worldPos = mul(pc.sceneData->invView, float4(viewPos, 1.0)).xyz;
            int probeIndex = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData->cameraWorldPos.xyz).indexA;
            if (probeIndex < 0) {
                srcPixel.xyz = float3(1.0, 0.0, 1.0);
            } else {
                uint h = (uint(probeIndex) + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 41: { // ReflectionProbeBinDisagreement: brute vs cell-lookup pick. Green = agree, red = bin lost a probe, yellow = different probe, magenta = bin-only (bug), gray = both empty.
            float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            if (depth <= 0.0) { srcPixel.xyz = float3(0.0); break; }
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 worldPos = mul(pc.sceneData->invView, float4(viewPos, 1.0)).xyz;
            int brute = PickReflectionProbe(worldPos, pc.reflectionProbes, pc.reflectionProbeCount).indexA;
            int binned = PickReflectionProbeBinned(worldPos, pc.reflectionProbes, pc.reflectionProbeCount, pc.worldGridProbeGrid, pc.sceneData->cameraWorldPos.xyz).indexA;
            if (brute < 0 && binned < 0) {
                srcPixel.xyz = float3(0.05);
            } else if (brute == binned) {
                srcPixel.xyz = float3(0.0, 1.0, 0.0);
            } else if (binned < 0 && brute >= 0) {
                srcPixel.xyz = float3(1.0, 0.0, 0.0);
            } else if (binned >= 0 && brute < 0) {
                srcPixel.xyz = float3(1.0, 0.0, 1.0);
            } else {
                srcPixel.xyz = float3(1.0, 1.0, 0.0);
            }
            break;
        }
        case 49: { // ReGIRCell: hue = hash of the pixel's ReGIR cell key, brightness = fill occupancy (cellData.y). Magenta = cell absent from the hash, black = sky.
            float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            if (depth <= 0.0 || pc.regirHashEntries == nullptr) { srcPixel.xyz = float3(0.0); break; }
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 worldPos = mul(pc.sceneData->invView, float4(viewPos, 1.0)).xyz;
            ReGIRHashKey key = ReGIRComputeKey(worldPos, pc.sceneData->cameraWorldPos.xyz);
            uint cellSlot = ReGIRHashFind(pc.regirHashEntries, key);
            if (cellSlot == REGIR_HASH_INVALID) {
                srcPixel.xyz = float3(1.0, 0.0, 1.0);
            } else {
                float occupancy = pc.regirCellData[cellSlot];
                uint h = ReGIRChecksum(key) * 2654435761u;
                float3 hue = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
                srcPixel.xyz = hue * max(occupancy, 0.05);
            }
            break;
        }
        case 50: { // ReGIRCellLight: colour of the light held by the most reservoirs in the pixel's ReGIR cell (emissive triangles counted per group), brightness = that share of the cell's reservoirs. Magenta = cell absent, black = sky.
            float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            if (depth <= 0.0 || pc.regirHashEntries == nullptr || pc.regirReservoirs == nullptr) { srcPixel.xyz = float3(0.0); break; }
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 worldPos = mul(pc.sceneData->invView, float4(viewPos, 1.0)).xyz;
            ReGIRHashKey key = ReGIRComputeKey(worldPos, pc.sceneData->cameraWorldPos.xyz);
            uint cellSlot = ReGIRHashFind(pc.regirHashEntries, key);
            if (cellSlot == REGIR_HASH_INVALID) { srcPixel.xyz = float3(1.0, 0.0, 1.0); break; }
            uint base = ReGIRCellReservoirBase(cellSlot);
            // Boyer-Moore majority over group-collapsed ids, then a second pass counts the winner
            uint candidate = ~0u;
            int votes = 0;
            for (uint r = 0u; r < REGIR_RESERVOIRS_PER_CELL; r++) {
                uint id = CollapseLightIdxToGroup(pc.regirReservoirs[base + r].lightIdx);
                if (id == ~0u) { continue; }
                if (votes == 0) { candidate = id; votes = 1; }
                else if (id == candidate) { votes++; }
                else { votes--; }
            }
            uint count = 0u;
            for (uint r = 0u; r < REGIR_RESERVOIRS_PER_CELL; r++) {
                if (CollapseLightIdxToGroup(pc.regirReservoirs[base + r].lightIdx) == candidate) { count++; }
            }
            srcPixel.xyz = LightIdxColor(candidate) * max(float(count) / float(REGIR_RESERVOIRS_PER_CELL), 0.05);
            break;
        }
        case 42: { // DofCoc: signed circle of confusion in dof_color_coc.a. Red = near field, blue = far, black = sharp; sqrt ramp over a 64px reference (the radius slider cap) so typical radii do not read black.
            float coc = srcPixel.a;
            srcPixel.xyz = float3(sqrt(saturate(-coc / 64.0)), 0.0, sqrt(saturate(coc / 64.0)));
            break;
        }
        case 43: { // DofTileMax: r = max near magnitude, g = max far, both full-res pixel radii. Red = near reach, blue = far reach, same 64px sqrt ramp as case 42.
            srcPixel.xyz = float3(sqrt(saturate(srcPixel.r / 64.0)), 0.0, sqrt(saturate(srcPixel.g / 64.0)));
            break;
        }
        case 44: { // AlphaOnly
            srcPixel.xyz = float3(srcPixel.a);
            break;
        }
        case 45: { // DofZones on dof_color_coc.a: green = in focus, orange ramp = near transition, red = near saturated, cyan ramp = far transition, blue = far saturated. Ramp brightness shows progress, so the transition corners read as bands.
            float coc = srcPixel.a;
            float nearR = max(f16tof32(pc.dofPackedRadii & 0xFFFFu), 1e-3);
            float farR = max(f16tof32(pc.dofPackedRadii >> 16), 1e-3);
            if (abs(coc) < 0.05) { srcPixel.xyz = float3(0.0, 0.35, 0.0); }
            else if (coc <= -nearR * 0.999) { srcPixel.xyz = float3(1.0, 0.0, 0.0); }
            else if (coc < 0.0) { srcPixel.xyz = lerp(float3(0.1, 0.05, 0.0), float3(1.0, 0.5, 0.0), -coc / nearR); }
            else if (coc >= farR * 0.999) { srcPixel.xyz = float3(0.0, 0.0, 1.0); }
            else { srcPixel.xyz = lerp(float3(0.0, 0.05, 0.1), float3(0.0, 0.8, 1.0), coc / farR); }
            break;
        }
        case 46: { // GTAOTemporalAO: gtao_temporal.x accumulated visibility (white open, black occluded); A/Bs against GTAO Filtered
            srcPixel.xyz = float3(srcPixel.x);
            break;
        }
        case 47: { // GTAOTemporalCount: gtao_temporal.y is count/64; black = fresh disocclusion, white = fully converged
            srcPixel.xyz = float3(srcPixel.y);
            break;
        }
        case 48: { // GTAOResolved: shadows_resolve_target.y, the AO term lighting multiplies
            srcPixel.xyz = float3(srcPixel.y);
            break;
        }
    }

#if WILL_EDITOR
    if (pc.readback != nullptr && pc.regirHashEntries != nullptr && pc.regirReservoirs != nullptr && all(threadID.xy == pc.cursorPixel)
        && pc.valueTransformationType >= 49u && pc.valueTransformationType <= 51u) {
        pc.readback->regirCursorValid = 0u;
        float depth = rdgTexturesFloat[pc.depthTextureIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
        if (depth > 0.0) {
            float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
            float3 viewPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
            float3 worldPos = mul(pc.sceneData->invView, float4(viewPos, 1.0)).xyz;
            ReGIRHashKey key = ReGIRComputeKey(worldPos, pc.sceneData->cameraWorldPos.xyz);
            uint cellSlot = ReGIRHashFind(pc.regirHashEntries, key);
            pc.readback->regirCursorLevel = key.level;
            pc.readback->regirCursorCell[0] = key.cellPos.x;
            pc.readback->regirCursorCell[1] = key.cellPos.y;
            pc.readback->regirCursorCell[2] = key.cellPos.z;
            pc.readback->regirCursorSlot = cellSlot;
            if (cellSlot != REGIR_HASH_INVALID) {
                uint base = ReGIRCellReservoirBase(cellSlot);
                uint topIdx[4] = { ~0u, ~0u, ~0u, ~0u };
                uint topCount[4] = { 0u, 0u, 0u, 0u };
                uint empty = 0u;
                uint other = 0u;
                for (uint r = 0u; r < REGIR_RESERVOIRS_PER_CELL; r++) {
                    uint id = CollapseLightIdxToGroup(pc.regirReservoirs[base + r].lightIdx);
                    if (id == ~0u) { empty++; continue; }
                    bool placed = false;
                    for (uint k = 0u; k < 4u && !placed; k++) {
                        if (topIdx[k] == id) { topCount[k]++; placed = true; }
                        else if (topIdx[k] == ~0u) { topIdx[k] = id; topCount[k] = 1u; placed = true; }
                    }
                    if (!placed) { other++; }
                }
                float3 centerVS = mul(pc.sceneData->view, float4(ReGIRCellCenterFromKey(key), 1.0)).xyz;
                float minDist2 = ReGIRTargetMinDist2(key.level);
                for (uint k = 0u; k < 4u; k++) {
                    pc.readback->regirCursorTopIdx[k] = topIdx[k];
                    pc.readback->regirCursorTopCount[k] = topCount[k];
                    float target = 0.0;
                    float3 pos = float3(0.0);
                    if (topIdx[k] != ~0u && pc.restirLightVS != nullptr) {
                        LightView vs = UnpackLightVS(pc.restirLightVS[topIdx[k]]);
                        target = EvalCellLightTarget(vs, centerVS, centerVS, ReGIRCellHalfDiag(key.level), UnpackUnorm4x8(vs.packedColor).rgb, minDist2);
                        pos = mul(pc.sceneData->invView, float4(vs.center, 1.0)).xyz;
                    }
                    pc.readback->regirCursorTopTarget[k] = target;
                    pc.readback->regirCursorTopPos[k * 3u + 0u] = pos.x;
                    pc.readback->regirCursorTopPos[k * 3u + 1u] = pos.y;
                    pc.readback->regirCursorTopPos[k * 3u + 2u] = pos.z;
                }
                pc.readback->regirCursorEmpty = empty;
                pc.readback->regirCursorOther = other;
                pc.readback->regirCursorOccupancy = pc.regirCellData[cellSlot];
                pc.readback->regirCursorValid = 1u;
            }
        }
    }
#endif
    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = srcPixel;
}
