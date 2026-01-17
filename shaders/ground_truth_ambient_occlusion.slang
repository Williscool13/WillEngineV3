import common_interop;
import push_constant_interop;
import shader_functions;
import constants_interop;

import bindless_declarations;


// ==================== Depth Prepass ===============================

float ScreenToViewSpaceDepth(float screenDepth, float depthLinearizeMul, float depthLinearizeAdd) {
    // Optimization by XeGTAO
    // https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli#L112
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

float ClampDepth(float depth){
    return clamp( depth, 0.0, 65504.0);
}

float DepthMipPrefilter(float depth0, float depth1, float depth2, float depth3, float effectRadius, float radiusMultiplier, float falloffRange) {
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    // https://github.com/GameTechDev/XeGTAO/blob/e7698f874e90f2516fca26c696ec3cd2c70e505a/Source/Rendering/Shaders/XeGTAO.hlsli#L583C13-L583C14
    const float depthRangeScaleFactor = 0.75f;// found empirically :)


    const float _effectRadius = depthRangeScaleFactor * effectRadius * radiusMultiplier;
    const float _falloffRange = falloffRange * _effectRadius;
    const float falloffFrom = _effectRadius * (1 - falloffRange);

    const float falloffMul = -1.0 /  _falloffRange;
    const float falloffAdd = falloffFrom / (_falloffRange) + 1.0;

    float weight0 = saturate((maxDepth-depth0) * falloffMul + falloffAdd);
    float weight1 = saturate((maxDepth-depth1) * falloffMul + falloffAdd);
    float weight2 = saturate((maxDepth-depth2) * falloffMul + falloffAdd);
    float weight3 = saturate((maxDepth-depth3) * falloffMul + falloffAdd);

    float weightSum = weight0 + weight1 + weight2 + weight3;
    return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) / weightSum;
}

groupshared float g_scratchDepths[8][8];

[shader("compute")]
[numthreads(GTAO_DEPTH_PREPASS_DISPATCH_X, GTAO_DEPTH_PREPASS_DISPATCH_Y, 1)]
void ComputeGTAODepthPrepass(uniform GTAODepthPrepassPushConstant pc, uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    // MIP 0
    const uint2 baseCoord = dtid.xy;
    const int2 screenPos = int2(baseCoord) * 2; // We process 2x2 pixels in MIP 0

    if (screenPos.x > pc.sceneData->mainRenderTargetSize.x || screenPos.y > pc.sceneData->mainRenderTargetSize.y) {
        return;
    }

    float2 uv = (float2(screenPos) + 0.5) * pc.sceneData->texelSize;

    float4 depths = rdgTextures[pc.inputDepth].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, int2(1, 1));
    float rDepth0 = depths.w;
    float rDepth1 = depths.z;
    float rDepth2 = depths.x;
    float rDepth3 = depths.y;

    float depth0 = ClampDepth(ScreenToViewSpaceDepth(rDepth0, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd));
    float depth1 = ClampDepth(ScreenToViewSpaceDepth(rDepth1, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd));
    float depth2 = ClampDepth(ScreenToViewSpaceDepth(rDepth2, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd));
    float depth3 = ClampDepth(ScreenToViewSpaceDepth(rDepth3, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd));

    rdgRenderTextures[pc.outputDepth0][screenPos + int2(0, 0)] = float4(depth0, 0.0f, 0.0f, 0.0f);
    rdgRenderTextures[pc.outputDepth0][screenPos + int2(1, 0)] = float4(depth1, 0.0f, 0.0f, 0.0f);
    rdgRenderTextures[pc.outputDepth0][screenPos + int2(0, 1)] = float4(depth2, 0.0f, 0.0f, 0.0f);
    rdgRenderTextures[pc.outputDepth0][screenPos + int2(1, 1)] = float4(depth3, 0.0f, 0.0f, 0.0f);

    // MIP 1
    float dm1 = DepthMipPrefilter(depth0, depth1, depth2, depth3, pc.effectRadius, pc.radiusMultiplier, pc.effectFalloffRange);
    rdgRenderTextures[pc.outputDepth1][baseCoord] = float4(dm1, 0.0f, 0.0f, 0.0f);
    g_scratchDepths[gtid.x][gtid.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    if (all(gtid.xy % 2u == uint2(0u))) {
        float inTL = g_scratchDepths[gtid.x+0][gtid.y+0];
        float inTR = g_scratchDepths[gtid.x+1][gtid.y+0];
        float inBL = g_scratchDepths[gtid.x+0][gtid.y+1];
        float inBR = g_scratchDepths[gtid.x+1][gtid.y+1];

        float dm2 = DepthMipPrefilter(inTL, inTR, inBL, inBR, pc.effectRadius, pc.radiusMultiplier, pc.effectFalloffRange);
        rdgRenderTextures[pc.outputDepth2][baseCoord/2u] = float4(dm2, 0.0f, 0.0f, 0.0f);
        g_scratchDepths[gtid.x][gtid.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    if (all(gtid.xy % 4u == uint2(0u))) {
        float inTL = g_scratchDepths[gtid.x+0][gtid.y+0];
        float inTR = g_scratchDepths[gtid.x+2][gtid.y+0];
        float inBL = g_scratchDepths[gtid.x+0][gtid.y+2];
        float inBR = g_scratchDepths[gtid.x+2][gtid.y+2];

        float dm3 = DepthMipPrefilter(inTL, inTR, inBL, inBR, pc.effectRadius, pc.radiusMultiplier, pc.effectFalloffRange);
        rdgRenderTextures[pc.outputDepth3][baseCoord/4u] = float4(dm3, 0.0f, 0.0f, 0.0f);
        g_scratchDepths[gtid.x][gtid.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    if (all(gtid.xy % 8u == uint2(0u))) {
        float inTL = g_scratchDepths[gtid.x+0][gtid.y+0];
        float inTR = g_scratchDepths[gtid.x+4][gtid.y+0];
        float inBL = g_scratchDepths[gtid.x+0][gtid.y+4];
        float inBR = g_scratchDepths[gtid.x+4][gtid.y+4];

        float dm4 = DepthMipPrefilter(inTL, inTR, inBL, inBR, pc.effectRadius, pc.radiusMultiplier, pc.effectFalloffRange);
        rdgRenderTextures[pc.outputDepth4][baseCoord/8u] = float4(dm4, 0.0f, 0.0f, 0.0f);
    }
}

// ==================== GTAO Main Pass ===============================

static const float XE_GTAO_PI = 3.1415926535897932384626433832795;
static const float XE_GTAO_PI_HALF = 1.5707963267948966192313216916398;
static const float XE_GTAO_OCCLUSION_TERM_SCALE = 1.5f;

static const uint XE_HILBERT_LEVEL = 6u;
static const uint XE_HILBERT_WIDTH = (1u << XE_HILBERT_LEVEL);
static const uint XE_HILBERT_AREA = (XE_HILBERT_WIDTH * XE_HILBERT_WIDTH);

// http://h14s.p5r.org/2012/09/0x5f3759df.html
float XeGTAO_FastSqrt(float x) {
    return asfloat(0x1fbd1df5 + (asuint(x) >> 1));
}

// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float XeGTAO_FastACos(float inX) {
    const float PI = 3.141593;
    const float HALF_PI = 1.570796;
    float x = abs(inX);
    float res = -0.156583 * x + HALF_PI;
    res *= XeGTAO_FastSqrt(1.0 - x);
    return (inX >= 0) ? res : PI - res;
}

uint XE_GTAO_HilbertIndex(uint posX, uint posY) {
    uint index = 0u;
    for (uint curLevel = XE_HILBERT_WIDTH / 2u; curLevel > 0u; curLevel /= 2u) {
        uint regionX = (posX & curLevel) > 0u ? 1u : 0u;
        uint regionY = (posY & curLevel) > 0u ? 1u : 0u;
        index += curLevel * curLevel * ((3u * regionX) ^ regionY);
        if (regionY == 0u) {
            if (regionX == 1u) {
                posX = uint(XE_HILBERT_WIDTH - 1u) - posX;
                posY = uint(XE_HILBERT_WIDTH - 1u) - posY;
            }
            uint temp = posX;
            posX = posY;
            posY = temp;
        }
    }
    return index;
}

float2 XE_GTAO_SpatioTemporalNoise(int2 pixCoord, uint temporalIndex) {
    uint index = XE_GTAO_HilbertIndex(uint(pixCoord.x), uint(pixCoord.y));
    index += 288u * (temporalIndex % 64u);
    return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

float4 XE_GTAO_CalculateDepthEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ) {
    float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - float4(centerZ, centerZ, centerZ, centerZ);

    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));

    return saturate(1.25 - edgesLRTB / (centerZ * 0.011));
}

float XeGTAO_PackEdges(float4 edgesLRTB) {
    // Pack 4 edge values into single float (each edge gets 6 bits precision)
    edgesLRTB = saturate(edgesLRTB) * 255.0;
    uint4 iEdges = uint4(edgesLRTB);
    // Simple packing - you might want to adjust this
    return float((iEdges.x << 0) | (iEdges.y << 8) | (iEdges.z << 16) | (iEdges.w << 24)) / float(0xFFFFFFFF);
}

[shader("compute")]
[numthreads(GTAO_MAIN_PASS_DISPATCH_X, GTAO_MAIN_PASS_DISPATCH_Y, 1)]
void ComputeGTAOMain(uniform GTAOMainPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    const int2 screenPos = int2(dtid.xy);

    if (screenPos.x >= pc.sceneData->mainRenderTargetSize.x || screenPos.y >= pc.sceneData->mainRenderTargetSize.y) {
        return;
    }

    float2 uv = (float2(screenPos) + 0.5) * pc.sceneData->texelSize;

    float2 gatherCenter = float2(screenPos) * pc.sceneData->texelSize;
    float4 valuesUL = rdgTextures[pc.prefilteredDepthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherCenter);
    float4 valuesBR = rdgTextures[pc.prefilteredDepthIndex].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], gatherCenter, int2(1, 1));

    float viewSpaceZM = valuesUL.y;
    const float viewSpaceZL = valuesUL.x;
    const float viewSpaceZT = valuesUL.z;
    const float viewSpaceZR = valuesBR.z;
    const float viewSpaceZB = valuesBR.x;

    // Calculate and store edge data
    float4 edges = XE_GTAO_CalculateDepthEdges(viewSpaceZM, viewSpaceZL, viewSpaceZR, viewSpaceZT, viewSpaceZB);
    float packedEdges = XeGTAO_PackEdges(edges);
    rdgRenderTextures[pc.edgeDataIndex][screenPos] = float4(packedEdges, 0, 0, 0);


    float3 viewNormal = rdgTextures[pc.normalBufferIndex].Sample(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv).rgb;
    viewNormal = viewNormal * 2.0 - 1.0;

    // Move center pixel slightly towards camera to avoid precision artifacts
    viewSpaceZM = viewSpaceZM * 0.998f;

    // Reconstruct view space position
    float3 vPos = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceZM);
    float3 viewVec = normalize(-vPos);

    // Prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
    viewNormal = normalize(viewNormal + max(0.0, -dot(viewNormal, viewVec)) * viewVec);

    const float effectRadius = pc.effectRadius * pc.radiusMultiplier;
    const float sampleDistributionPower = pc.sampleDistributionPower;
    const float thinOccluderCompensation = pc.thinOccluderCompensation;
    const float falloffRange = pc.effectFalloffRange * effectRadius;
    const float falloffFrom = effectRadius * (1.0 - pc.effectFalloffRange);

    // Fadeout precompute optimization
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float visibility = 0.0;

    // Noise generation
    float2 noise = XE_GTAO_SpatioTemporalNoise(screenPos, pc.noiseIndex);
    float noiseSlice = noise.x;
    float noiseSample = noise.y;

    const float pixelTooCloseThreshold = 1.3;
    const float2 pixelDirRBViewspaceSizeAtCenterZ = viewSpaceZM.xx * pc.sceneData->ndcToViewMulXPixelSize;

    float screenspaceRadius = effectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;

    visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

    if (screenspaceRadius < pixelTooCloseThreshold) {
        visibility = 1.0;
        visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
        rdgRenderTextures[pc.aoOutputIndex][screenPos] = float4(visibility, 0, 0, 0);
        return;
    }

    const float minS = pixelTooCloseThreshold / screenspaceRadius;

    float sliceCount = pc.sliceCount;
    float stepsPerSlice = pc.stepsPerSlice;

    for (float slice = 0; slice < sliceCount; slice++) {
        float sliceK = (slice + noiseSlice) / sliceCount;
        float phi = sliceK * XE_GTAO_PI;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        float2 omega = float2(cosPhi, -sinPhi);

        omega *= screenspaceRadius;

        const float3 directionVec = float3(cosPhi, sinPhi, 0);
        const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
        const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

        float3 projectedNormalVec = viewNormal - axisVec * dot(viewNormal, axisVec);
        float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
        float projectedNormalVecLength = length(projectedNormalVec);
        float cosNorm = saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);
        float n = signNorm * XeGTAO_FastACos(cosNorm);

        const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
        const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

        float horizonCos0 = lowHorizonCos0;
        float horizonCos1 = lowHorizonCos1;

        for (float step = 0; step < stepsPerSlice; step++) {
            const float stepBaseNoise = (slice + step * stepsPerSlice) * 0.6180339887498948482;
            float stepNoise = frac(noiseSample + stepBaseNoise);

            float s = (step + stepNoise) / stepsPerSlice;
            s = pow(s, sampleDistributionPower);
            s += minS;

            float2 sampleOffset = s * omega;
            float sampleOffsetLength = length(sampleOffset);

            const int XE_GTAO_DEPTH_MIP_LEVELS = 5;
            const float mipLevel = clamp(log2(sampleOffsetLength) - pc.depthMipSamplingOffset, 0.0, float(XE_GTAO_DEPTH_MIP_LEVELS));

            sampleOffset = round(sampleOffset) * pc.sceneData->texelSize;

            float2 sampleScreenPos0 = uv + sampleOffset;
            float SZ0 = rdgTextures[pc.prefilteredDepthIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], sampleScreenPos0, mipLevel).r;
            float3 samplePos0 = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, sampleScreenPos0, SZ0);

            float2 sampleScreenPos1 = uv - sampleOffset;
            float SZ1 = rdgTextures[pc.prefilteredDepthIndex].SampleLevel(rdgSamplers[RDG_POINT_SAMPLER_INDEX], sampleScreenPos1, mipLevel).r;
            float3 samplePos1 = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, sampleScreenPos1, SZ1);

            float3 sampleDelta0 = samplePos0 - vPos;
            float3 sampleDelta1 = samplePos1 - vPos;
            float sampleDist0 = length(sampleDelta0);
            float sampleDist1 = length(sampleDelta1);

            float3 sampleHorizonVec0 = sampleDelta0 / sampleDist0;
            float3 sampleHorizonVec1 = sampleDelta1 / sampleDist1;

            float falloffBase0 = length(float3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
            float falloffBase1 = length(float3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
            float weight0 = saturate(falloffBase0 * falloffMul + falloffAdd);
            float weight1 = saturate(falloffBase1 * falloffMul + falloffAdd);

            float shc0 = dot(sampleHorizonVec0, viewVec);
            float shc1 = dot(sampleHorizonVec1, viewVec);

            shc0 = lerp(lowHorizonCos0, shc0, weight0);
            shc1 = lerp(lowHorizonCos1, shc1, weight1);

            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
        }

        projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);

        float h0 = -XeGTAO_FastACos(horizonCos1);
        float h1 = XeGTAO_FastACos(horizonCos0);
        float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
        float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
        float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
        visibility += localVisibility;
    }

    visibility /= sliceCount;
    visibility = pow(visibility, pc.finalValuePower);
    visibility = max(0.03, visibility);

    visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
    rdgRenderTextures[pc.aoOutputIndex][screenPos] = float4(visibility, 0, 0, 0);
}