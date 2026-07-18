import common_interop;
import push_constant_interop;
import lights_interop;
import restir_common;

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeReSTIRDITransformLights(uniform ReSTIRTransformLightsPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint lightIdx = threadID.x;
    if (lightIdx >= uint(pc.lightData->lightCount)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    LightView vs = TransformLightToView(pc.lightData->lights[lightIdx], scene.view);
    pc.lightVS[lightIdx] = PackLightVS(vs);
}
