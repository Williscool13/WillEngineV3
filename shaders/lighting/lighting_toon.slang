import push_constant_interop;
import shader_functions;
import constants_interop;
import model_interop;
import visibility_lighting_common;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform LightingResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(pixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float3 N = normalize(li.viewNormal);
    float3 L = normalize(mul((float3x3)pc.sceneData[pc.sceneDataIndex].view, normalize(-pc.lightData->mainLightDirection.xyz)));

    float shadowFactor = li.shadows.x;
    float gtao = li.shadows.y;

    // Remap [-1,1] to [0,1] so dark-side normals get bands too
    float nDotL = dot(N, L) * 0.5 + 0.5;

    const float toonBands = 5.0;
    float toonDiffuse = floor(clamp(nDotL, 0.0, 1.0) * toonBands) / toonBands;
    toonDiffuse *= shadowFactor;

    const float lightIntensity = pc.lightData->mainLightDirection.w;
    const float3 lightColor = pc.lightData->mainLightColor.rgb;
    float3 directLight = li.albedo.xyz * toonDiffuse * lightIntensity * lightColor;

    float3 ambient = li.albedo.xyz * 0.15 * gtao;

    float3 finalColor = directLight + ambient + li.emissiveRGB;
    rdgStorageFloat4[pc.outputImageIndex][pixel] = float4(finalColor, 1.0);
}
