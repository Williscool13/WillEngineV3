import push_constant_interop;
import shader_functions;
import shadow_functions;
import constants_interop;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ShadowsResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.x || threadID.y >= pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData[pc.sceneDataIndex].mainRenderTargetSize);
    float depth = rdgTextures[pc.depthIndex].Load(int3(threadID.xy, 0)).r;

    if (depth <= 0.0) {
        rdgStorageFloat2[pc.outputImageIndex][threadID.xy] = float2(1.0, 1.0);
        return;
    }

    float3 viewNormal = rdgTextures[pc.normalIndex].Load(int3(threadID.xy, 0)).xyz;
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData[pc.sceneDataIndex].depthLinearizeMult, pc.sceneData[pc.sceneDataIndex].depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData[pc.sceneDataIndex].ndcToViewMul, pc.sceneData[pc.sceneDataIndex].ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(viewNormal * 2.0 - 1.0);

    // CSM shadow factor
    float shadowFactor;
    if (any(pc.csmIndices == int4(-1, -1, -1, -1))) {
        shadowFactor = 1.0f;
    } else {
        shadowFactor = GetShadowFactor(viewPosition, N, pc.sceneData[pc.sceneDataIndex], pc.shadowData, pc.csmIndices, rdgTextures, rdgSamplers[RDG_POINT_SAMPLER_INDEX], rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX]);
    }

    // GTAO
    float gtao;
    if (pc.gtaoFilteredIndex >= 0) {
        gtao = rdgTextures[pc.gtaoFilteredIndex].Load(int3(threadID.xy, 0)).x;
    } else {
        gtao = 1.0f;
    }

    rdgStorageFloat2[pc.outputImageIndex][threadID.xy] = float2(shadowFactor, gtao);
}