import push_constant_interop;
import constants_interop;

import bindless_declarations;

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
        case 0: // Sampled 4 channel float
            srcPixel.xyzw = rdgTexturesFloat4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rgba;
            break;
        case 1: // Sampled 2 channel float
            srcPixel.xy = rdgTexturesFloat4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).rg;
            break;
        case 2: // Sampled 1 channel float
            float c = rdgTexturesFloat4[pc.textureIndexInArray].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0).r;
            srcPixel.xyz = float3(c);
            break;
        case 3: // 4 channel int
            uint4 v = rdgTexturesUInt4[pc.textureIndexInArray].Load(int3(texCoord, 0)).rgba;
            srcPixel.xyzw = float4(v);
            break;
        case 4: // 2 channel int
            uint2 b = rdgTexturesUInt2[pc.textureIndexInArray].Load(int3(texCoord, 0)).rg;
            srcPixel = float4(b, 0.0f, 1.0f);
            break;
        case 5: // 1 channel int
            uint n = rdgTexturesUInt[pc.textureIndexInArray].Load(int3(texCoord, 0)).r;
            srcPixel.x = float(n);
            break;
        case 6:
            uint4 yes = rdgStorageUInt4[pc.textureIndexInArray].Load(texCoord).rgba;
            srcPixel.xyzw = float4(yes);
            break;
        case 7:
            uint v1 = rdgStorageUInt[pc.textureIndexInArray].Load(texCoord).r;
            srcPixel.xyz = float3(v1);
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
            uint triIdx = uint(srcPixel.x) & 0xFF;
            uint h = (triIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 8: { // VisBuffMeshlet - bits [8:31] of vis.x
            if (srcPixel.x == 0.0 && srcPixel.y == 0.0) { break; }
            uint meshletIdx = uint(srcPixel.x) >> 8;
            uint h = (meshletIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
        case 9: { // VisBuffInstance - vis.y
            if (srcPixel.x == 0.0 && srcPixel.y == 0.0) { break; }
            uint instIdx = uint(srcPixel.y);
            uint h = (instIdx + 1u) * 2654435761u;
            srcPixel.xyz = float3(float(h >> 24), float((h >> 16) & 0xFF), float((h >> 8) & 0xFF)) / 255.0;
            break;
        }
    }

    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = srcPixel;
}