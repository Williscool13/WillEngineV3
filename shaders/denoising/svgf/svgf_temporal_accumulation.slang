import common_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;
import constants_interop;

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFTemporalAccumulationMain(uniform SVGFTemporalAccumulationPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 uv = (float2(pixel) + 0.5) * texelSize;

    const float currentDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    const float4 currentColor = rdgTexturesFloat4[pc.colorIndex].Load(int3(pixel, 0));
    const float currentLum = Luminance(currentColor.rgb);

    if (currentDepth == 0.0) {
        rdgStorageFloat4[pc.outputColorIndex][pixel] = currentColor;
        rdgStorageFloat2[pc.outputMomentsIndex][pixel] = float2(currentLum, currentLum * currentLum);
        rdgStorageFloat[pc.outputHistoryLengthIndex][pixel] = 0.0;
        rdgStorageFloat[pc.outputVarianceIndex][pixel] = 0.0;
        return;
    }

    // Reproject
    const uint4 g1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    const float2 velocityNDC = UnpackMotionVectors(g1.g);
    const float2 historyUV = uv + velocityNDC * 0.5;
    const bool inBounds = all(historyUV >= float2(0.0)) && all(historyUV <= float2(1.0));

    bool validHistory = inBounds;

    uint2 historyPixel = uint2(0, 0);
    if (inBounds) {
        historyPixel = clamp(uint2(historyUV * float2(pc.width, pc.height)), uint2(0, 0), uint2(pc.width - 1, pc.height - 1));

        // Normal disocclusion
        const float3 currentN = normalize(UnpackNormalOct16(g1.r));
        const float3 historyN = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(historyPixel, 0)).r));
        if (dot(currentN, historyN) < 0.906) {
            validHistory = false;
        }

        // Depth disocclusion
        if (validHistory) {
            SceneData sd = *pc.sceneData;
            const float viewDepthCurrent = ScreenToViewSpaceDepth(currentDepth, sd.depthLinearizeMult, sd.depthLinearizeAdd);
            const float rawReprojDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(historyPixel, 0)).r;
            const float viewDepthReproj = ScreenToViewSpaceDepth(rawReprojDepth, sd.depthLinearizeMult, sd.depthLinearizeAdd);
            if (abs(viewDepthReproj - viewDepthCurrent) > 0.1 * abs(viewDepthCurrent)) {
                validHistory = false;
            }
        }
    }

    const float gradient = rdgTexturesFloat[pc.gradientIndex].Load(int3(pixel, 0)).r;
    float historyLength = 0.0;
    if (validHistory) {
        historyLength = rdgTexturesFloat[pc.historyLengthIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUV, 0).r;
    }

    if (pc.gradientThreshold > 0.0) {
        const float resetFactor = clamp((gradient - pc.gradientThreshold) / pc.gradientThreshold, 0.0, 1.0);
        historyLength *= (1.0 - resetFactor);
    }
    historyLength = min(historyLength + 1.0, 256.0);

    float alpha = clamp(max(pc.alphaMin, 1.0 / historyLength), 0.0, 1.0);

    // Accumulate
    float4 blendedColor = currentColor;
    float2 blendedMoments = float2(currentLum, currentLum * currentLum);

    if (validHistory) {
        const float4 historyColor = rdgTexturesFloat4[pc.colorHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUV, 0);
        const float2 historyMoments = rdgTexturesFloat2[pc.momentsHistoryIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], historyUV, 0);
        blendedColor = lerp(historyColor, currentColor, alpha);
        blendedMoments = lerp(historyMoments, float2(currentLum, currentLum * currentLum), alpha);
    }

    const float variance = max(0.0, blendedMoments.y - blendedMoments.x * blendedMoments.x);

    rdgStorageFloat4[pc.outputColorIndex][pixel] = blendedColor;
    rdgStorageFloat2[pc.outputMomentsIndex][pixel] = blendedMoments;
    rdgStorageFloat[pc.outputHistoryLengthIndex][pixel] = historyLength;
    rdgStorageFloat[pc.outputVarianceIndex][pixel] = variance;
}
