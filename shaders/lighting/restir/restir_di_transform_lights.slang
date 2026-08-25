import common_interop;
import push_constant_interop;
import lights_interop;
import light_index_common;
import restir_common;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeReSTIRDITransformLights(uniform ReSTIRTransformLightsPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    int analyticCount = pc.lightData->analyticLightCount;
    if (threadID.x >= LiveLightCount(pc.lightData->lightCount, analyticCount)) {
        return;
    }
    uint lightIdx = LiveLightIndex(threadID.x, analyticCount);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    LightView vs = TransformLightToView(pc.lightData->lights[lightIdx], scene.view);
    pc.lightVS[lightIdx] = PackLightVS(vs);
}
