import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;
import shader_functions;
import packing_functions;
import gi_gather_common;

// Full-res resolve: 4-tap Poisson bilateral upsample of the gather SH evaluated at the pixel normal, then counter temporal New=(n*Prev+Curr)/(n+1) via matrix reprojection.

static const float2 GI_UPSCALE_POISSON[4] = { float2(-0.75, -0.25), float2(0.25, -0.75), float2(0.75, 0.25), float2(-0.25, 0.75) };
static const float GI_UPSCALE_PLANE_DIST_SIGMA = 0.01;
static const float GI_UPSCALE_NORMAL_POWER = 32.0;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeGIUpscale(uniform GIUpscalePushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    if (depth <= 0.0 || (gt1.w & 1u) == 0u) {
        rdgStorageFloat4[pc.outputIndex][pixel] = float4(0.0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float3 centerNormal = UnpackNormalOct16(gt1.r);
    float3 worldN = normalize(mul((float3x3)scene.invView, centerNormal));
    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float centerViewZ = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 centerXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, centerViewZ);
    float invPlaneSigma = 1.0 / (GI_UPSCALE_PLANE_DIST_SIGMA * abs(centerViewZ));

    float4 shR = float4(0.0);
    float4 shG = float4(0.0);
    float4 shB = float4(0.0);
    float sumW = 0.0;
    float2 halfCoord = (float2(pixel) + 0.5) * 0.5;
    [unroll]
    for (uint i = 0; i < 4; i++) {
        int2 tap = clamp(int2(floor(halfCoord + GI_UPSCALE_POISSON[i])), int2(0), int2(pc.gatherExtent) - 1);
        uint2 tapFull = GIGatherRepresentativePixel(uint2(tap), pc.frameIndex, pc.renderExtent);
        float tapDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(tapFull, 0)).r;
        uint4 tapGt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(tapFull, 0));
        if (tapDepth <= 0.0 || (tapGt1.w & 1u) == 0u) { continue; }

        float tapViewZ = ScreenToViewSpaceDepth(tapDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float2 tapUv = (float2(tapFull) + 0.5) / float2(pc.renderExtent);
        float3 tapXv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, tapUv, tapViewZ);
        float3 tapNormal = UnpackNormalOct16(tapGt1.r);

        float w = exp(-abs(dot(centerNormal, tapXv - centerXv)) * invPlaneSigma);
        w *= pow(saturate(dot(centerNormal, tapNormal)), GI_UPSCALE_NORMAL_POWER);

        shR += rdgTexturesFloat4[pc.shRIndex].Load(int3(tap, 0)) * w;
        shG += rdgTexturesFloat4[pc.shGIndex].Load(int3(tap, 0)) * w;
        shB += rdgTexturesFloat4[pc.shBIndex].Load(int3(tap, 0)) * w;
        sumW += w;
    }

    float3 current;
    if (sumW > 1e-4) {
        float invW = 1.0 / sumW;
        current = max(float3(shR.x + dot(shR.yzw, worldN), shG.x + dot(shG.yzw, worldN), shB.x + dot(shB.yzw, worldN)) * invW, float3(0.0));
    } else {
        // All taps rejected: plain bilinear.
        current = GIGatherIndirectDiffuse(pc.shRIndex, pc.shGIndex, pc.shBIndex, uv, worldN);
    }

    float3 result = current;
    float accum = 1.0;
    if (pc.bHistoryValid != 0u) {
        float3 worldPos = mul(scene.invView, float4(centerXv, 1.0)).xyz;
        float4 prevClip = mul(scene.prevViewProj, float4(worldPos, 1.0));
        if (prevClip.w > 0.0) {
            float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
            if (all(prevUV >= float2(0.0)) && all(prevUV <= float2(1.0))) {
                int2 prevPixel = clamp(int2(prevUV * float2(pc.renderExtent)), int2(0), int2(pc.renderExtent) - int2(1));
                float prevRawDepth = rdgTexturesFloat[pc.depthHistoryIndex].Load(int3(prevPixel, 0)).r;
                if (prevRawDepth > 0.0) {
                    float prevViewDepth = ScreenToViewSpaceDepth(prevRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                    if (abs(prevViewDepth - prevClip.w) <= 0.03 * prevClip.w + 0.05) {
                        float3 prevWorldN = normalize(mul(transpose(float3x3(scene.prevView)), UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneHistoryIndex].Load(int3(prevPixel, 0)).r)));
                        if (dot(worldN, prevWorldN) > 0.5) {
                            float4 history = rdgTexturesFloat4[pc.historyIndex].Load(int3(prevPixel, 0));
                            float n = min(history.a, GI_GATHER_MAX_ACCUM - 1.0);
                            result = (n * history.rgb + current) / (n + 1.0);
                            accum = n + 1.0;
                        }
                    }
                }
            }
        }
    }

    rdgStorageFloat4[pc.outputIndex][pixel] = float4(result, accum);
}
