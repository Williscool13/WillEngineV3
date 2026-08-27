import common_interop;
import push_constant_interop;
import bindless_declarations;
import shader_functions;
import gi_gather_common;

// R16 count channel stores count / GTAO_TEMPORAL_COUNT_SCALE.
static const float GTAO_TEMPORAL_COUNT_SCALE = 64.0;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadowsResolve(uniform ShadowsResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint2 pixel = threadID.xy;
    SceneData scene = pc.sceneData[0];
    if (pixel.x >= scene.mainRenderTargetSize.x || pixel.y >= scene.mainRenderTargetSize.y) {
        return;
    }

    float gtao = 1.0;
    if (pc.gtaoFilteredIndex >= 0) {
        gtao = rdgTexturesFloat[pc.gtaoFilteredIndex].Load(int3(pixel, 0)).x;
    }

    float ao = gtao;
    float count = 1.0;
    if (pc.temporalOutputIndex != ~0x0u && pc.bHistoryValid != 0u) {
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
        uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
        if (depth > 0.0 && (gt1.w & 1u) != 0u) {
            uint2 renderExtent = uint2(scene.mainRenderTargetSize);
            float2 uv = (float2(pixel) + 0.5) / scene.mainRenderTargetSize;
            float3 centerNormal = UnpackNormalOct16(gt1.r);
            float3 worldN = normalize(mul((float3x3)scene.invView, centerNormal));
            float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
            float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
            float NoV = saturate(abs(dot(centerNormal, normalize(centerXv))));
            GIGatherScalarHistory history = GIGatherFetchScalarHistory(scene, pc.historyIndex, pc.depthHistoryIndex, pc.gbufferOneHistoryIndex, uv, gt1, centerViewZ, NoV, worldN, renderExtent);
            if (history.quality > 0.0) {
                float n = min(history.count * GTAO_TEMPORAL_COUNT_SCALE, pc.temporalMaxAccum - 1.0) * history.quality;
                ao = (n * history.value + gtao) / (n + 1.0);
                count = n + 1.0;
            }
        }
    }

    rdgStorageFloat2[pc.outputImageIndex][pixel] = float2(1.0, ao);
    if (pc.temporalOutputIndex != ~0x0u) {
        rdgStorageFloat2[pc.temporalOutputIndex][pixel] = float2(ao, count / GTAO_TEMPORAL_COUNT_SCALE);
    }
}
