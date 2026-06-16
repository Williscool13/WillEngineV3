import common_interop;
import push_constant_interop;
import constants_interop;
import lights_interop;
import restir_interop;

import packing_functions;
import shader_functions;
import visibility_lighting_common;
import bindless_declarations;
import restir_common;

groupshared float s_sum[256];
groupshared float s_count[256];

[numthreads(16, 16, 1)]
void ComputeMain(uniform ReSTIRDIBoilingPushConstant pc, uint3 threadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID) {
    uint2 pixel = threadID.xy;
    uint flat = groupThreadID.y * 16u + groupThreadID.x;
    bool inBounds = all(pixel < pc.renderExtent);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint pixelIdx = inBounds ? (pixel.y * pc.renderExtent.x + pixel.x) : 0u;
    Reservoir r = inBounds ? pc.reservoirBuffer[pixelIdx] : ReservoirEmpty();

    float metric = 0.0;
    bool valid = false;

    if (inBounds && r.lightIdx != ~0u && r.W > 0.0 && r.lightIdx < uint(pc.lightData->areaLightCount)) {
        uint2 fullPixel = QuadFullPixel(pixel, pc.pixelScale, pc.frameIndex, pc.depthIndex, pc.gbufferOneIndex, 1.0 / float2(scene.mainRenderTargetSize), scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
        if (depth > 0.0) {
            LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, ~0x0, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
            if (li.valid) {
                float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
                float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);
                float3 N = normalize(li.viewNormal);
                float3 V = normalize(-viewPosition);
                float roughness = max(li.roughness, 0.045);
                float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);

                AreaLightVS vs = UnpackAreaLightVS(pc.lightVS[r.lightIdx]);
                float3 yStar = DecodeSamplePos(r, vs);
                AreaLightSample s = EvalAreaLightSample(vs, yStar, viewPosition, N, vs.range);
                float3 lightColor = UnpackUnorm4x8(vs.packedColor).rgb;
                float p_hat = EvalPHat(s, V, N, li.albedo.xyz, F0, li.metallic, roughness, lightColor, vs.intensity);
                metric = p_hat * r.W;
                valid = metric > 0.0;
            }
        }
    }

    s_sum[flat] = valid ? metric : 0.0;
    s_count[flat] = valid ? 1.0 : 0.0;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128u; stride > 0u; stride >>= 1u) {
        if (flat < stride) {
            s_sum[flat] += s_sum[flat + stride];
            s_count[flat] += s_count[flat + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (!inBounds) { return; }

    float mean = (s_count[0] > 0.0) ? (s_sum[0] / s_count[0]) : 0.0;
    float multiplier = 10.0 / clamp(pc.filterStrength, 1e-6, 1.0) - 9.0;
    if (valid && mean > 0.0 && metric > mean * multiplier) {
        r.W = 0.0;
    }

    pc.outputBuffer[pixelIdx] = r;
}
