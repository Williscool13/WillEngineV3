import common_interop;
import push_constant_interop;
import lights_interop;
import restir_common;

[numthreads(64, 1, 1)]
void ComputeMain(uniform ReSTIRTransformLightsPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    uint lightIdx = threadID.x;
    if (lightIdx >= uint(pc.lightData->areaLightCount)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    AreaLightVS vs = TransformAreaLight(pc.lightData->areaLights[lightIdx], scene.view);
    pc.lightVS[lightIdx] = PackAreaLightVS(vs);
}
