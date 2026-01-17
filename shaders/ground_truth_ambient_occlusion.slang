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
    // kind of redundant, the view space depth can only be as far as the depth buffer (which is 1000.0f at time of writing)
    return clamp( depth, 0.0, 3.402823466e+38 );
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

groupshared float scratchDepths[8][8];
[numthreads(16, 16, 1)]
void ComputeGTAODepthPrepass(uniform GTAODepthPrepassPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    int2 groupThreadID = int2(threadID.xy);

    // MIP 0
    const int2 baseCoord = threadID.xy;
    const int2 screenPos = int2(baseCoord.xy) * 2; // We process 2x2 pixels in MIP 0

    if (screenPos.x > pc.sceneData->mainRenderTargetSize.x || screenPos.y > pc.sceneData->mainRenderTargetSize.y) {
        return;
    }

    float2 uv = (float2(screenPos) + 0.5) * pc.sceneData->texelSize;

    float4 depths = rdgTextures[pc.inputDepth].GatherRed(rdgSamplers[RDG_POINT_SAMPLER_INDEX], uv, int2(1, 1));
    float rDepth0 = depths.w;
    float rDepth1 = depths.z;
    float rDepth2 = depths.x;
    float rDepth3 = depths.y;

    float depth0 = ClampDepth(ScreenToViewSpaceDepth(rDepth0, pc.depthLinearizeMult, pushConstants.depthLinearizeAdd));
    float depth1 = ClampDepth(ScreenToViewSpaceDepth(rDepth1, pc.depthLinearizeMult, pushConstants.depthLinearizeAdd));
    float depth2 = ClampDepth(ScreenToViewSpaceDepth(rDepth2, pc.depthLinearizeMult, pushConstants.depthLinearizeAdd));
    float depth3 = ClampDepth(ScreenToViewSpaceDepth(rDepth3, pc.depthLinearizeMult, pushConstants.depthLinearizeAdd));

    imageStore(outDepth0, screenPos + ivec2(0, 0), vec4(depth0));
    imageStore(outDepth0, screenPos + ivec2(1, 0), vec4(depth1));
    imageStore(outDepth0, screenPos + ivec2(0, 1), vec4(depth2));
    imageStore(outDepth0, screenPos + ivec2(1, 1), vec4(depth3));

    if (pushConstants.debug == 1){
        imageStore(debugImage, screenPos + ivec2(0, 0), vec4(vec3(depth0 / 1000.0f), 1.0f));
        imageStore(debugImage, screenPos + ivec2(1, 0), vec4(vec3(depth1 / 1000.0f), 1.0f));
        imageStore(debugImage, screenPos + ivec2(0, 1), vec4(vec3(depth2 / 1000.0f), 1.0f));
        imageStore(debugImage, screenPos + ivec2(1, 1), vec4(vec3(depth3 / 1000.0f), 1.0f));
    }

    // MIP 1
    float dm1 = depthMipFilter(depth0, depth1, depth2, depth3, pushConstants.effectRadius, pushConstants.radiusMultiplier, pushConstants.effectFalloffRange);
    imageStore(outDepth1, ivec2(baseCoord), vec4(dm1));
    g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

    memoryBarrierShared();
    barrier();

    // MIP 2
    if (all(equal(groupThreadID.xy % 2u, uvec2(0u)))){
        float inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = g_scratchDepths[groupThreadID.x+1][groupThreadID.y+0];
        float inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+1];
        float inBR = g_scratchDepths[groupThreadID.x+1][groupThreadID.y+1];

        float dm2 = depthMipFilter(inTL, inTR, inBL, inBR, pushConstants.effectRadius, pushConstants.radiusMultiplier, pushConstants.effectFalloffRange);
        imageStore(outDepth2, ivec2(baseCoord/2u), vec4(dm2));
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
    }

    memoryBarrierShared();
    barrier();

    // MIP 3
    if (all(equal(groupThreadID.xy % 4u, uvec2(0u)))){
        float inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = g_scratchDepths[groupThreadID.x+2][groupThreadID.y+0];
        float inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+2];
        float inBR = g_scratchDepths[groupThreadID.x+2][groupThreadID.y+2];

        float dm3 = depthMipFilter(inTL, inTR, inBL, inBR, pushConstants.effectRadius, pushConstants.radiusMultiplier, pushConstants.effectFalloffRange);
        imageStore(outDepth3, ivec2(baseCoord/4u), vec4(dm3));
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
    }

    memoryBarrierShared();
    barrier();

    // MIP 4
    if (all(equal(groupThreadID.xy % 8u, uvec2(0u)))){
        float inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        float inTR = g_scratchDepths[groupThreadID.x+4][groupThreadID.y+0];
        float inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+4];
        float inBR = g_scratchDepths[groupThreadID.x+4][groupThreadID.y+4];

        float dm4 = depthMipFilter(inTL, inTR, inBL, inBR, pushConstants.effectRadius, pushConstants.radiusMultiplier, pushConstants.effectFalloffRange);
        imageStore(outDepth4, ivec2(baseCoord/8u), vec4(dm4));
        //g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm4;
    }
}