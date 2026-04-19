import push_constant_interop;
import shader_functions;
import shadow_functions;
import constants_interop;

import bindless_declarations;

[numthreads(16, 16, 1)]
void ComputeMain(uniform ShadowsResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    if (threadID.x >= pc.sceneData->mainRenderTargetSize.x || threadID.y >= pc.sceneData->mainRenderTargetSize.y) return;

    float2 uv = (float2(threadID.xy) + 0.5) / float2(pc.sceneData->mainRenderTargetSize);
    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(threadID.xy, 0)).r;

    if (depth <= 0.0) {
        rdgStorageFloat2[pc.outputImageIndex][threadID.xy] = float2(1.0, 1.0);
        return;
    }

    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(threadID.xy, 0));
    float3 viewNormal = UnpackNormalOct16(gt1.g);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, pc.sceneData->depthLinearizeMult, pc.sceneData->depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(pc.sceneData->ndcToViewMul, pc.sceneData->ndcToViewAdd, uv, viewSpaceDepth);
    float3 N = normalize(viewNormal);

    // CSM shadow factor
    float shadowFactor;
    if (any(pc.csmIndices == int4(-1, -1, -1, -1))) {
        shadowFactor = 1.0f;
    } else {
        float viewSpaceDepth = abs(viewPosition.z);
        if (viewSpaceDepth < pc.shadowData->nearSplits[0] || viewSpaceDepth > pc.shadowData->farSplits[SHADOW_CASCADE_COUNT - 1]) {
            shadowFactor = 1.0f;
        } else {
            int cascadeLevel = SHADOW_CASCADE_COUNT - 1;
            for (int i = 0; i < SHADOW_CASCADE_COUNT - 1; ++i) {
                if (viewSpaceDepth < pc.shadowData->farSplits[i]) {
                    cascadeLevel = i;
                    break;
                }
            }

            float baseOffset = 0.01f;
            float offset = baseOffset * (1 << cascadeLevel);
            float3 offsetPosition = viewPosition + N * offset;
            float4 offsetWorldPosition = mul(pc.sceneData->invView, float4(offsetPosition, 1.0f));
            float3 worldPos = offsetWorldPosition.xyz;

            float4 lightSpacePos = mul(pc.shadowData->lightSpaceMatrices[cascadeLevel], float4(worldPos, 1.0));
            float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
            projCoords.xy = projCoords.xy * 0.5 + 0.5;

            uint shadowMapIndex = pc.csmIndices[cascadeLevel];
            Texture2D<float> shadowMap = rdgTexturesFloat[NonUniformResourceIndex(shadowMapIndex)];

            float lightSize = pc.shadowData->lightSizes[cascadeLevel];
            int pcfSamples = int(pc.shadowData->pcfSamples[cascadeLevel]);

            float shadowFactor1 = PCF(shadowMap, rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], projCoords.xy, projCoords.z, lightSize, pcfSamples);

            float outShadowFactor = shadowFactor1;
            if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
                float blendStart = pc.shadowData->farSplits[cascadeLevel] * 0.98;
                float blendEnd = pc.shadowData->farSplits[cascadeLevel];
                if (viewSpaceDepth >= blendStart) {
                    float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
                    outShadowFactor = lerp(shadowFactor1, 1.0, blendFactor);
                }
            } else {
                float blendStart = pc.shadowData->nearSplits[cascadeLevel + 1];
                float blendEnd = pc.shadowData->farSplits[cascadeLevel];
                if (viewSpaceDepth >= blendStart) {
                    float4 nextLightSpacePos = mul(pc.shadowData->lightSpaceMatrices[cascadeLevel + 1], float4(worldPos, 1.0));
                    float3 nextProjCoords = nextLightSpacePos.xyz / nextLightSpacePos.w;
                    nextProjCoords.xy = nextProjCoords.xy * 0.5 + 0.5;

                    uint nextShadowMapIndex = pc.csmIndices[cascadeLevel + 1];
                    Texture2D<float> nextShadowMap = rdgTexturesFloat[NonUniformResourceIndex(nextShadowMapIndex)];

                    float nextLightSize = pc.shadowData->lightSizes[cascadeLevel + 1];
                    int nextPcfSamples = int(pc.shadowData->pcfSamples[cascadeLevel + 1]);

                    float shadowFactor2 = PCF(nextShadowMap, rdgCompareSamplers[RDG_LINEAR_DEPTH_SAMPLER_INDEX], nextProjCoords.xy, nextProjCoords.z, nextLightSize, nextPcfSamples);

                    float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
                    outShadowFactor = lerp(shadowFactor1, shadowFactor2, blendFactor);
                }
            }

            shadowFactor = lerp(pc.shadowData->shadowIntensity, 1.0, outShadowFactor);
        }
    }

    // GTAO
    float gtao;
    if (pc.gtaoFilteredIndex >= 0) {
        gtao = rdgTexturesFloat[pc.gtaoFilteredIndex].Load(int3(threadID.xy, 0)).x;
    } else {
        gtao = 1.0f;
    }

    rdgStorageFloat2[pc.outputImageIndex][threadID.xy] = float2(shadowFactor, gtao);
}