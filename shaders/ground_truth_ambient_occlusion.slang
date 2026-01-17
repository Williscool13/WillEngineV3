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