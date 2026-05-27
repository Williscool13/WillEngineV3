import push_constant_interop;
import constants_interop;
import restir_interop;

import bindless_declarations;
import shader_functions;

[[vk::push_constant]]
DebugVisualizePushConstant pc;

[numthreads(16, 16, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.dstExtent.x || threadID.y >= pc.dstExtent.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.dstExtent);

    int3 sampleTexCoord = int3(threadID.xy, 0);
    int2 texCoord = uint2(threadID.xy);

    float4 srcPixel = float4(1.0f, 0.0f, 0.0f, 1.0f);
    switch (pc.textureArrayIndex) {
        case 0: // Sampled float4
            srcPixel.xyzw = rdgTexturesFloat4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgba;
            break;
        case 1: // Sampled float2
            srcPixel.xy = rdgTexturesFloat2[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rg;
            break;
        case 2: // Sampled float
            float c = rdgTexturesFloat[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
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
            srcPixel.x = float(rdgTexturesUInt[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r);
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
            srcPixel = UnpackAlbedoRGB8(asuint(srcPixel.x));
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
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirBuffer[pixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 18: { // ReservoirTemporalLightIdx - hash-color the selected light index from the temporal reservoir buffer
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirTemporalBuffer[pixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 19: { // ReservoirTemporalW - visualize W from temporal reservoir; remapped via W/(W+1)
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirTemporalBuffer[pixelIdx];
            float w = (r.lightIdx != ~0u) ? r.W / (r.W + 1.0) : 0.0;
            srcPixel.xyz = float3(w, w, w);
            break;
        }
        case 20: { // ReservoirGenerateW - visualize W from generate reservoir; remapped via W/(W+1)
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirBuffer[pixelIdx];
            float w = (r.lightIdx != ~0u) ? r.W / (r.W + 1.0) : 0.0;
            srcPixel.xyz = float3(w, w, w);
            break;
        }
        case 21: { // ReservoirSpatialLightIdx - hash-color the selected light index from the spatial reservoir buffer
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirSpatialBuffer[pixelIdx];
            if (r.lightIdx == ~0u) {
                srcPixel.xyz = float3(0.0, 0.0, 0.0);
            } else {
                uint h = (r.lightIdx + 1u) * 2654435761u;
                srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            }
            break;
        }
        case 22: { // ReservoirSpatialW - visualize W from spatial reservoir; remapped via W/(W+1)
            uint pixelIdx = uint(threadID.y) * uint(pc.dstExtent.x) + uint(threadID.x);
            Reservoir r = pc.reservoirSpatialBuffer[pixelIdx];
            float w = (r.lightIdx != ~0u) ? r.W / (r.W + 1.0) : 0.0;
            srcPixel.xyz = float3(w, w, w);
            break;
        }
    }

    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = srcPixel;
}
