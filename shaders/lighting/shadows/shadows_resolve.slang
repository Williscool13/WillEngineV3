import push_constant_interop;
import bindless_declarations;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadowsResolve(uniform ShadowsResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData->mainRenderTargetSize.x || threadID.y >= pc.sceneData->mainRenderTargetSize.y) return;

    float gtao = 1.0f;
    if (pc.gtaoFilteredIndex >= 0) {
        gtao = rdgTexturesFloat[pc.gtaoFilteredIndex].Load(int3(threadID.xy, 0)).x;
    }

    rdgStorageFloat2[pc.outputImageIndex][threadID.xy] = float2(1.0f, gtao);
}
