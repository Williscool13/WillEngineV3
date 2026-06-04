import common_interop;
import constants_interop;
import push_constant_interop;

import bindless_declarations;

import shader_constants;

[shader("fragment")]
float4 FragmentSkybox(uniform EnvironmentSkyboxPushConstant pc, FullscreenPassVertexOutput input) : SV_Target0
{
    if (pc.cubemapIndex == -1) {
        return float4(0.1, 0.2, 0.0, 1.0);
    }
    SceneData scene = pc.sceneData[pc.sceneDataIndex];

    float2 uv = input.uv;
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldPos = mul(scene.invViewProj, float4(ndc, 1.0, 1.0));
    float3 rayDir = normalize(worldPos.xyz / worldPos.w - scene.cameraWorldPos.xyz);
    rayDir.y = -rayDir.y;

    int mip = pc.skyboxLOD < 4 ? pc.skyboxLOD : ENVIRONMENT_MAP_DIFFUSE_MIP;
    return assetCubemaps[pc.cubemapIndex].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], rayDir, mip);
}