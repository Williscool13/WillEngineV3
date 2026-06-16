import common_interop;
import push_constant_interop;
import lights_interop;
import restir_common;

[numthreads(64, 1, 1)]
void ComputeMain(uniform ReSTIRTransformLightsPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint lightIdx = threadID.x;
    if (lightIdx >= uint(pc.lightData->lightCount)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    LightVS vs = TransformLight(pc.lightData->lights[lightIdx], scene.view);
    pc.lightVS[lightIdx] = PackLightVS(vs);
}
