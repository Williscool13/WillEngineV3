// BC6H UF16 encoder, mode 11 only (one endpoint pair, 10-bit endpoints, 4-bit indices).
// Derivative work of extern/GPURealTimeBC6H/compress.hlsl by Krzysztof Narkowicz, see NOTICES.txt.
// Corresponds to the upstream P1 path with INSET_COLOR_BBOX and OPTIMIZE_ENDPOINTS enabled; P2 modes are not ported.

import common_interop;
import constants_interop;
import push_constant_interop;

[[vk::binding(4, 0)]]
RWTexture2DArray<half4> finalCubemaps[];

static const float HALF_MAX = 65504.0;

float3 HalfBits(float3 x)
{
    return float3(f32tof16(x.x), f32tof16(x.y), f32tof16(x.z));
}

float3 FromHalfBits(float3 bits)
{
    return float3(f16tof32(uint(bits.x)), f16tof32(uint(bits.y)), f16tof32(uint(bits.z)));
}

float3 Quantize10(float3 x)
{
    return (HalfBits(x) * 1024.0) / (float(0x7bff) + 1.0);
}

uint ComputeIndex4(float texelPos, float endPoint0Pos, float endPoint1Pos)
{
    const float range = endPoint1Pos - endPoint0Pos;
    if (abs(range) < 1e-6) {
        return 0;
    }
    return uint(clamp((texelPos - endPoint0Pos) / range * 14.93333 + 0.03333 + 0.5, 0.0, 15.0));
}

/** Pulls the endpoints in toward the second-most-extreme texel per channel, capped at 1/32 of the block's log range, so a lone outlier cannot stretch the palette. */
void InsetColorBBox(float3 texels[16], inout float3 blockMin, inout float3 blockMax)
{
    float3 refinedMin = blockMax;
    float3 refinedMax = blockMin;
    for (uint i = 0; i < 16; ++i) {
        const float3 isMin = step(texels[i], blockMin);
        const float3 isMax = step(blockMax, texels[i]);
        refinedMin = min(refinedMin, texels[i] * (1.0 - isMin) + refinedMin * isMin);
        refinedMax = max(refinedMax, texels[i] * (1.0 - isMax) + refinedMax * isMax);
    }

    float3 logMin = log2(blockMin + 1.0);
    float3 logMax = log2(blockMax + 1.0);
    const float3 logRefinedMin = log2(refinedMin + 1.0);
    const float3 logRefinedMax = log2(refinedMax + 1.0);
    const float3 logExtent = (logMax - logMin) * (1.0 / 32.0);

    logMin += min(logRefinedMin - logMin, logExtent);
    logMax -= min(logMax - logRefinedMax, logExtent);

    blockMin = exp2(logMin) - 1.0;
    blockMax = exp2(logMax) - 1.0;
}

/** Least-squares refit of the endpoints against the indices the current endpoints produce, clamped back into the original bounding box. */
void OptimizeEndpoints(float3 texels[16], float3 nonInsetMin, float3 nonInsetMax, inout float3 blockMin, inout float3 blockMax)
{
    float3 blockDir = blockMax - blockMin;
    const float dirSum = blockDir.x + blockDir.y + blockDir.z;
    if (dirSum <= 0.0) {
        return;
    }
    blockDir /= dirSum;

    const float endPoint0Pos = f32tof16(dot(blockMin, blockDir));
    const float endPoint1Pos = f32tof16(dot(blockMax, blockDir));

    float3 alphaTexelSum = float3(0.0);
    float3 betaTexelSum = float3(0.0);
    float alphaBetaSum = 0.0;
    float alphaSqSum = 0.0;
    float betaSqSum = 0.0;

    for (uint i = 0; i < 16; ++i) {
        const uint texelIndex = ComputeIndex4(f32tof16(dot(texels[i], blockDir)), endPoint0Pos, endPoint1Pos);
        const float beta = saturate(float(texelIndex) / 15.0);
        const float alpha = 1.0 - beta;
        const float3 texelBits = HalfBits(texels[i]);

        alphaTexelSum += alpha * texelBits;
        betaTexelSum += beta * texelBits;
        alphaBetaSum += alpha * beta;
        alphaSqSum += alpha * alpha;
        betaSqSum += beta * beta;
    }

    const float det = alphaSqSum * betaSqSum - alphaBetaSum * alphaBetaSum;
    if (abs(det) <= 0.00001) {
        return;
    }

    const float detRcp = 1.0 / det;
    blockMin = clamp(FromHalfBits(clamp(detRcp * (alphaTexelSum * betaSqSum - betaTexelSum * alphaBetaSum), 0.0, HALF_MAX)), nonInsetMin, nonInsetMax);
    blockMax = clamp(FromHalfBits(clamp(detRcp * (betaTexelSum * alphaSqSum - alphaTexelSum * alphaBetaSum), 0.0, HALF_MAX)), nonInsetMin, nonInsetMax);
}

[shader("compute")]
[numthreads(ENVIRONMENT_MAP_GENERATION_DISPATCH_X, ENVIRONMENT_MAP_GENERATION_DISPATCH_Y, 1)]
void ComputeEncodeBC6H(uniform BC6HEncodePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    const uint blocksX = (pc.width + 3) / 4;
    const uint blocksY = (pc.height + 3) / 4;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }

    float3 texels[16];
    for (uint i = 0; i < 16; ++i) {
        const uint x = min(tid.x * 4 + (i & 3), pc.width - 1);
        const uint y = min(tid.y * 4 + (i >> 2), pc.height - 1);
        const half4 texel = finalCubemaps[pc.sourceIndex][int3(int(x), int(y), int(pc.face))];
        texels[i] = clamp(float3(texel.rgb), 0.0, HALF_MAX);
    }

    float3 blockMin = texels[0];
    float3 blockMax = texels[0];
    for (uint i = 1; i < 16; ++i) {
        blockMin = min(blockMin, texels[i]);
        blockMax = max(blockMax, texels[i]);
    }

    const float3 nonInsetMin = blockMin;
    const float3 nonInsetMax = blockMax;
    InsetColorBBox(texels, blockMin, blockMax);
    OptimizeEndpoints(texels, nonInsetMin, nonInsetMax, blockMin, blockMax);

    float3 blockDir = blockMax - blockMin;
    const float dirSum = blockDir.x + blockDir.y + blockDir.z;
    blockDir = dirSum > 0.0 ? blockDir / dirSum : float3(1.0 / 3.0);

    float3 endpoint0 = clamp(Quantize10(blockMin), 0.0, 1023.0);
    float3 endpoint1 = clamp(Quantize10(blockMax), 0.0, 1023.0);
    float endPoint0Pos = f32tof16(dot(blockMin, blockDir));
    float endPoint1Pos = f32tof16(dot(blockMax, blockDir));

    // The anchor index drops its high bit, so texel 0 must land in the lower half of the palette
    if (ComputeIndex4(f32tof16(dot(texels[0], blockDir)), endPoint0Pos, endPoint1Pos) > 7) {
        const float3 swapEndpoint = endpoint0;
        endpoint0 = endpoint1;
        endpoint1 = swapEndpoint;
        const float swapPos = endPoint0Pos;
        endPoint0Pos = endPoint1Pos;
        endPoint1Pos = swapPos;
    }

    uint indices[16];
    for (uint i = 0; i < 16; ++i) {
        indices[i] = ComputeIndex4(f32tof16(dot(texels[i], blockDir)), endPoint0Pos, endPoint1Pos);
    }
    // Rounding can land the anchor back on 8 when it sits exactly at the midpoint; only 3 bits are stored for it
    indices[0] = min(indices[0], 7);

    const uint3 e0 = uint3(endpoint0);
    const uint3 e1 = uint3(endpoint1);

    uint4 block;
    block.x = 0x03;
    block.x |= e0.x << 5;
    block.x |= e0.y << 15;
    block.x |= e0.z << 25;
    block.y = e0.z >> 7;
    block.y |= e1.x << 3;
    block.y |= e1.y << 13;
    block.y |= e1.z << 23;
    block.z = e1.z >> 9;
    block.z |= indices[0] << 1;
    block.z |= indices[1] << 4;
    block.z |= indices[2] << 8;
    block.z |= indices[3] << 12;
    block.z |= indices[4] << 16;
    block.z |= indices[5] << 20;
    block.z |= indices[6] << 24;
    block.z |= indices[7] << 28;
    block.w = indices[8];
    block.w |= indices[9] << 4;
    block.w |= indices[10] << 8;
    block.w |= indices[11] << 12;
    block.w |= indices[12] << 16;
    block.w |= indices[13] << 20;
    block.w |= indices[14] << 24;
    block.w |= indices[15] << 28;

    pc.blocks[tid.y * blocksX + tid.x] = block;
}
