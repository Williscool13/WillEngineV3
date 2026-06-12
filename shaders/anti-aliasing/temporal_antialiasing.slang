import common_interop;
import push_constant_interop;
import shader_functions;
import constants_interop;

import bindless_declarations;

[[vk::push_constant]]
TemporalAntialiasingPushConstant pc;

static const int TILE_DIM = 16;
static const int APRON_DIM = TILE_DIM + 2;
static const int APRON_TEXEL_COUNT = APRON_DIM * APRON_DIM;

groupshared float3 gsColorYCoCg[APRON_DIM][APRON_DIM];
groupshared uint gsPackedVelocity[APRON_DIM][APRON_DIM];
groupshared float gsDepth[APRON_DIM][APRON_DIM];

// Clips history toward the box center instead of clamping per component; clamping moves colors to box corners and shifts hue
float3 ClipToNeighborhood(float3 history, float3 minc, float3 maxc) {
    float3 center = 0.5 * (maxc + minc);
    float3 extents = 0.5 * (maxc - minc) + 1e-6;
    float3 delta = history - center;
    float t = max(abs(delta.x) / extents.x, max(abs(delta.y) / extents.y, abs(delta.z) / extents.z));
    if (t > 1.0) {
        return center + delta / t;
    }
    return history;
}

// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/ and help from AI
float3 SampleHistoryCatmullRom(float2 uv) {
    float2 texelSize = pc.sceneData.texelSize;

    float2 samplePos = uv / texelSize;
    float2 texelCenter = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texelCenter;

    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    float2 w3 = 0.5 * f3 - 0.5 * f2;

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    // Clamp taps half a texel inside the image so the outer taps cannot streak edge content during fast motion
    float2 minUv = 0.5 * texelSize;
    float2 maxUv = 1.0 - 0.5 * texelSize;
    float2 tc0 = clamp((texelCenter - 1.0) * texelSize, minUv, maxUv);
    float2 tc12 = clamp((texelCenter + offset12) * texelSize, minUv, maxUv);
    float2 tc3 = clamp((texelCenter + 2.0) * texelSize, minUv, maxUv);

    float3 color =
        rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
        rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
        rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], tc12, 0).rgb                  * (w12.x * w12.y) +
        rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
        rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);

    // The 4 dropped corner taps carry (w0.x + w3.x) * (w0.y + w3.y) of the full separable weight; renormalize or history darkens up to ~1.6% at half-pixel phase
    float totalWeight = 1.0 - (w0.x + w3.x) * (w0.y + w3.y);
    return RGBToYCoCg(color / totalWeight);
}

float ComputeVelocityDisocclusion(float2 uv, float2 currentVelocityUV) {
    float2 historyUv = uv + currentVelocityUV;

    if (any(historyUv < float2(0.0)) || any(historyUv > float2(1.0))) {
        return 1.0;
    }

    int2 historyCoord = int2(historyUv * float2(pc.sceneData.mainRenderTargetSize));
    uint4 histG1 = rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(historyCoord, 0));
    float2 previousVelocityNDC = UnpackMotionVectors(histG1.g);
    float2 previousVelocityUV = previousVelocityNDC * 0.5;

    float velocityDelta = length(previousVelocityUV - currentVelocityUV);
    float disocclusion = saturate((velocityDelta - 0.001) * 10.0);

    return disocclusion;
}

[numthreads(TILE_DIM, TILE_DIM, 1)]
void ComputeMain(uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
    int2 renderSize = int2(pc.sceneData.mainRenderTargetSize);
    int2 groupOrigin = int2(groupID.xy) * TILE_DIM - 1;

    for (int i = int(groupIndex); i < APRON_TEXEL_COUNT; i += TILE_DIM * TILE_DIM) {
        int2 apronCoord = int2(i % APRON_DIM, i / APRON_DIM);
        int2 loadCoord = clamp(groupOrigin + apronCoord, int2(0), renderSize - 1);
        gsColorYCoCg[apronCoord.y][apronCoord.x] = RGBToYCoCg(rdgTexturesFloat4[pc.colorResolvedIndex].Load(int3(loadCoord, 0)).rgb);
        gsPackedVelocity[apronCoord.y][apronCoord.x] = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(loadCoord, 0)).g;
        gsDepth[apronCoord.y][apronCoord.x] = rdgTexturesFloat[pc.depthIndex].Load(int3(loadCoord, 0)).r;
    }
    GroupMemoryBarrierWithGroupSync();

    if (threadID.x >= uint(renderSize.x) || threadID.y >= uint(renderSize.y)) {
        return;
    }

    int2 texelCoord = int2(threadID.xy);
    float2 uv = (float2(texelCoord) + 0.5) * pc.sceneData.texelSize;
    int2 local = int2(groupThreadID.xy) + 1;

    float3 mean = float3(0.0);
    float3 squaredMean = float3(0.0);
    float3 minColor = float3(1e30);
    float3 maxColor = float3(-1e30);
    float closestDepth = -1.0;
    uint closestPackedVelocity = 0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float3 c = gsColorYCoCg[local.y + y][local.x + x];
            mean += c;
            squaredMean += c * c;
            minColor = min(minColor, c);
            maxColor = max(maxColor, c);

            // Reversed-Z: larger depth is closer. Foreground silhouettes must win the dilation, not the fastest mover
            float depth = gsDepth[local.y + y][local.x + x];
            if (depth > closestDepth) {
                closestDepth = depth;
                closestPackedVelocity = gsPackedVelocity[local.y + y][local.x + x];
            }
        }
    }
    mean /= 9.0;
    squaredMean /= 9.0;

    float3 variance = max(squaredMean - mean * mean, float3(0.0));
    float3 stddev = sqrt(variance);

    float3 gamma = float3(0.75, 1.0, 1.0);
    float3 minc = max(mean - gamma * stddev, minColor);
    float3 maxc = min(mean + gamma * stddev, maxColor);

    float2 velocityNDC = UnpackMotionVectors(closestPackedVelocity);
    float2 velocity = velocityNDC * 0.5;
    float2 historyUv = uv + velocity;
    bool validHistory = all(historyUv >= float2(0.0)) && all(historyUv <= float2(1.0));

    float3 currentYCoCg = gsColorYCoCg[local.y][local.x];
    float3 resultYCoCg;

    if (!validHistory) {
        // No usable history: pull toward the neighborhood mean as cheap spatial anti-aliasing instead of outputting raw jittered color
        resultYCoCg = lerp(currentYCoCg, mean, 0.5);
    } else {
        float velocityDisocclusion = ComputeVelocityDisocclusion(uv, velocity);
        float3 historyYCoCg = SampleHistoryCatmullRom(historyUv);
        historyYCoCg = ClipToNeighborhood(historyYCoCg, minc, maxc);

        float alpha = lerp(0.0625, 1.0, velocityDisocclusion);
        float lumaDiff = saturate(abs(currentYCoCg.x - historyYCoCg.x) / max(currentYCoCg.x, 0.2));
        // Luma mismatch may only increase responsiveness; without the max an already-high alpha would be pulled back down toward 0.5
        float boostedAlpha = max(alpha, min(alpha * 2.0, 0.5));
        alpha = lerp(alpha, boostedAlpha, lumaDiff);

        resultYCoCg = lerp(historyYCoCg, currentYCoCg, alpha);
    }

    float4 result = float4(YCoCgToRGB(resultYCoCg), 1.0);
    rdgStorageFloat4[pc.outputImageIndex][threadID.xy] = result;
    rdgStorageFloat4[pc.outputCopyIndex][threadID.xy] = result;
}
