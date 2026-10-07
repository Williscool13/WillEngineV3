import common_interop;
import constants_interop;
import push_constant_interop;
import shadow_interop;
import bindless_declarations;
import shader_functions;

static const float CSM_PCF_MARGIN_TEXELS = 1.5;

// R = sqrt(visibility) to match the ray traced sun shadow, G = (cascade + 1) / CSM_MAX_CASCADES, 0 outside every cascade.
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeCSMResolve(uniform CSMResolvePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth <= 0.0 || (gt1.w & 1u) == 0u) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPos = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewZ);
    float3 worldPos = mul(scene.invView, float4(viewPos, 1.0)).xyz;
    float3 worldN = normalize(mul((float3x3)scene.invView, UnpackNormalOct16(gt1.r)));

    float margin = 1.0 - CSM_PCF_MARGIN_TEXELS * 2.0 / float(pc.csmData->resolution);
    for (uint c = 0; c < pc.csmData->cascadeCount; ++c) {
        CSMCascadeGPU cascade = pc.csmData->cascades[c];
        float3 offsetPos = worldPos + worldN * (cascade.texelWorldSize * pc.csmData->normalOffsetTexels);
        float3 clip = mul(cascade.viewProj, float4(offsetPos, 1.0)).xyz;
        if (any(abs(clip.xy) > margin) || clip.z < 0.0 || clip.z > 1.0) {
            continue;
        }

        float2 atlasUv = (clip.xy * 0.5 + 0.5) * cascade.atlasScaleOffset.xy + cascade.atlasScaleOffset.zw;
        float2 texel = 1.0 / float2(pc.atlasExtent);
        float visibility = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                visibility += rdgTexturesFloat[pc.atlasIndex].SampleCmpLevelZero(rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], atlasUv + float2(x, y) * texel, clip.z);
            }
        }
        visibility /= 9.0;
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(sqrt(visibility), float(c + 1) / float(CSM_MAX_CASCADES));
        return;
    }

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, 0.0);
}
