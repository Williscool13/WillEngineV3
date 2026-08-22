import push_constant_interop;
import constants_interop;
import restir_interop;

import bindless_declarations;
import shader_functions;
import denoiser_common;
import restir_common;
import reflection_probe_common;

[[vk::push_constant]]
DebugVisualizePushConstant pc;

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

    bool bCurrentReservoir = bPixelInRenderBounds && IsActiveCheckerboardPixel(int2(pixelXY), false, pc.checkerboardField);
    uint2 currentReservoirPos = PixelPosToReservoirPos(pixelXY, pc.checkerboardField);
    uint currentStride = RestirCheckerboardStride(uint(pc.srcExtent.x), pc.checkerboardField);
    uint reservoirPixelIdx = currentReservoirPos.y * currentStride + currentReservoirPos.x;
    bool bInReservoirBounds = bCurrentReservoir;

    bool bHistoryReservoir = bPixelInRenderBounds && IsActiveCheckerboardPixel(int2(pixelXY), false, pc.historyCheckerboardField);
    uint2 historyReservoirPos = PixelPosToReservoirPos(pixelXY, pc.historyCheckerboardField);
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
            Reservoir r = pc.reservoirBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 18: { // ReservoirTemporalLightIdx - hash-color the selected light index from the temporal reservoir buffer
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirTemporalBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 19: { // ReservoirTemporalW - visualize W with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirTemporalBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W < 100.0) { float t = r.W / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W < 500.0) { float t = (r.W - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 20: { // ReservoirGenerateW - visualize W with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W < 100.0) { float t = r.W / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W < 500.0) { float t = (r.W - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 21: { // ReservoirSpatialLightIdx - hash-color the selected light index from the spatial reservoir buffer
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirSpatialBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 22: { // ReservoirSpatialW - visualize W with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bInReservoirBounds) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirSpatialBuffer[reservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W < 100.0) { float t = r.W / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W < 500.0) { float t = (r.W - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
            break;
        }
        case 23: { // ReservoirHistoryLightIdx - hash-color the selected light index from the history reservoir buffer
            if (!bHistoryReservoir) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirHistoryBuffer[historyReservoirPixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 24: { // ReservoirHistoryW - visualize W with range coloring: red [0,100), blue [100,500), green [500,inf)
            if (!bHistoryReservoir) { srcPixel.xyz = float3(0.0, 0.0, 0.0); break; }
            Reservoir r = pc.reservoirHistoryBuffer[historyReservoirPixelIdx];
            if (r.lightIdx == ~0u) { srcPixel.xyz = float3(0.0, 0.0, 0.0); }
            else if (r.W < 100.0) { float t = r.W / 100.0; srcPixel.xyz = float3(t, 0.0, 0.0); }
            else if (r.W < 500.0) { float t = (r.W - 100.0) / 400.0; srcPixel.xyz = float3(0.0, 0.0, t); }
            else { float t = min((r.W - 500.0) / 500.0, 1.0); srcPixel.xyz = float3(0.0, t, 0.0); }
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
    }

    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = srcPixel;
}
