import push_constant_interop;
import constants_interop;

import bindless_declarations;
import shader_functions;

[[vk::push_constant]]
DebugVisualizePushConstant pc;

float LinearizeDepth(float depth) {
    float zNear = pc.nearPlane;
    float zFar = pc.farPlane;
    float flipped = 1.0 - depth;
    return (2.0 * zNear) / (zFar + zNear - flipped * (zFar - zNear));
}

[numthreads(16, 16, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.dstExtent.x || threadID.y >= pc.dstExtent.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.dstExtent);
    uint2 texCoord = uint2(threadID.xy);

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
        case 3: // Sampled uint4, bitcast to preserve exact bits for unpacking
            uint4 v = rdgTexturesUInt4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgba;
            srcPixel = asfloat(v);
            break;
        case 4: // Sampled uint2, bitcast to preserve exact bits for unpacking
            uint2 b = rdgTexturesUInt2[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rg;
            srcPixel.xy = asfloat(b);
            break;
        case 5: // Sampled uint
            srcPixel.x = float(rdgTexturesUInt[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r);
            break;
        case 6: // Storage uint4, bitcast to preserve exact bits for unpacking
            srcPixel = asfloat(rdgStorageUInt4[pc.textureIndexInArray].Load(texCoord));
            break;
        case 7: // Storage uint
            srcPixel.xyz = float3(rdgStorageUInt[pc.textureIndexInArray].Load(texCoord).r);
            break;
        case 8: // Storage float4
            srcPixel = rdgStorageFloat4[pc.textureIndexInArray].Load(texCoord);
            break;
        case 9: // Storage float2
            srcPixel.xy = rdgStorageFloat2[pc.textureIndexInArray].Load(texCoord);
            break;
        case 10: // Storage uint2, bitcast to preserve exact bits for unpacking
            srcPixel.xy = asfloat(rdgStorageUInt2[pc.textureIndexInArray].Load(texCoord));
            break;
    }

    switch (pc.valueTransformationType) {
        case 1: // DepthRemap
            srcPixel.x = LinearizeDepth(srcPixel.x);
            srcPixel.y = LinearizeDepth(srcPixel.y);
            srcPixel.z = LinearizeDepth(srcPixel.z);
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
            if (srcPixel.x == 0.0 && srcPixel.y == 0.0) { break; }
            uint triIdx = asuint(srcPixel.x) & 0xFF;
            uint h = (triIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 8: { // VisBuffMeshlet - bits [8:31] of vis.x
            if (srcPixel.x == 0.0 && srcPixel.y == 0.0) { break; }
            uint meshletIdx = asuint(srcPixel.x) >> 8;
            uint h = (meshletIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 9: { // VisBuffInstance - vis.y
            if (srcPixel.x == 0.0 && srcPixel.y == 0.0) { break; }
            uint instIdx = asuint(srcPixel.y);
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
    }

    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = srcPixel;
}
