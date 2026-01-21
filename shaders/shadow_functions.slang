module shadow_functions;

import common_interop;
import constants_interop;
import shadows_interop;
import shader_constants;

float DepthToZ(float depth, float n, float f) {
	// return n * f / (depth * (n - f) + f); (normal depth buffer)
	return n * f / (f - depth * (f - n));
}

float PCF(Texture2D shadowMap, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float filterRadius, int32_t sampleCount) {
	float sum = 0.0;
	for (int i = 0; i < sampleCount; ++i) {
		float2 offset;
		switch (sampleCount) {
		    case 8:
    		    offset = POISSON_DISK_8[i];
    		    break;
		    case 16:
		        offset = POISSON_DISK_16[i];
		        break;
    		case 32:
    		    offset = POISSON_DISK_32[i];
    		    break;
    		case 64:
    		    offset = POISSON_DISK_64[i];
    		    break;
            default:
                offset = POISSON_DISK_64[i];
                break;
		}

		sum += shadowMap.SampleCmpLevelZero(comparisonSampler, uv + (offset * filterRadius), receiverDepth);
	}
	return sum / sampleCount;
}

float FindBlockerDistance(Texture2D shadowMap, SamplerState sampler, float2 uv, float receiverDepth, float nearPlane, float farPlane, float searchRadius, float lightSize)
{
    float blockerSum = 0.0;
    int blockerCount = 0;

	for (int i = 0; i < 16; ++i) {
        float2 offset = POISSON_DISK_16[i] * searchRadius;
        float shadowDepth = shadowMap.SampleLevel(sampler, uv + offset, 0).r;

        if (shadowDepth >= receiverDepth) {
            float blockerViewDepth = DepthToZ(shadowDepth, nearPlane, farPlane);
            blockerSum += blockerViewDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) {
        return -1.0;
    }

    return blockerSum / float(blockerCount);
}

float PCSS(Texture2D shadowMap, SamplerState sampler, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float receiverViewSpaceDepth, float lightSize, float nearPlane, float farPlane, int pcfSamples)
{
    // Blocker search
    float searchRadius = lightSize * (receiverViewSpaceDepth - nearPlane) / receiverViewSpaceDepth;
    float avgBlockerDepth = FindBlockerDistance(shadowMap, sampler, uv, receiverDepth, nearPlane, farPlane, searchRadius, lightSize);

    if (avgBlockerDepth < 0.0) {
        return 1.0f;
    }

    // Penumbra estimation
    float penumbra = (receiverViewSpaceDepth - avgBlockerDepth) / avgBlockerDepth;
    float filterRadius = penumbra * lightSize * nearPlane / receiverViewSpaceDepth;
    filterRadius = clamp(filterRadius, 0.001, 0.05);


    return PCF(shadowMap, comparisonSampler, uv, receiverDepth, filterRadius, pcfSamples);
}

public float GetShadowFactor(float3 viewPos, float3 viewNormal, SceneData* sceneData, ShadowData* shadowData, int4 csmIndices,
                            Texture2D textures[], SamplerState sampler, SamplerComparisonState comparisonSampler)
{
    float viewSpaceDepth = abs(viewPos.z);
    if (viewSpaceDepth < shadowData->nearSplits[0]) {
        return 0.0f;
    }
    if (viewSpaceDepth > shadowData.farSplits[SHADOW_CASCADE_COUNT - 1]) {
        return 1.0f;
    }

    int cascadeLevel = SHADOW_CASCADE_COUNT - 1;
    for (int i = 0; i < SHADOW_CASCADE_COUNT - 1; ++i) {
        if (viewSpaceDepth < shadowData.farSplits[i]) {
            cascadeLevel = i;
            break;
        }
    }

    float baseOffset = 0.01f;
    float offset = baseOffset * (1 << cascadeLevel);
    float3 offsetPosition = viewPos + viewNormal * offset;
    float4 offsetWorldPosition = mul(sceneData.invView, float4(offsetPosition, 1.0f));
    float3 worldPos = offsetWorldPosition.xyz;

    float4 lightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    uint shadowMapIndex = csmIndices[cascadeLevel];
    Texture2D shadowMap = textures[NonUniformResourceIndex(shadowMapIndex)];

    float lightSize = shadowData->lightSizes[cascadeLevel];
    int pcfSamples = int(shadowData->pcfSamples[cascadeLevel]);

    float shadowFactor1 = PCF(shadowMap, comparisonSampler, projCoords.xy, projCoords.z, lightSize, pcfSamples);
    float cascadeNear = shadowData.nearSplits[cascadeLevel];
    float cascadeFar = shadowData.farSplits[cascadeLevel];
    // float shadowFactor1 = PCSS(shadowMap, sampler, comparisonSampler, projCoords.xy, projCoords.z, viewSpaceDepth, lightSize, cascadeNear, cascadeFar, pcfSamples);

    float outShadowFactor = shadowFactor1;
    // Cascade blending
    if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
        float blendStart = shadowData.farSplits[cascadeLevel] * 0.98;
        float blendEnd = shadowData.farSplits[cascadeLevel];
        if (viewSpaceDepth >= blendStart) {
            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            outShadowFactor = lerp(shadowFactor1, 1.0, blendFactor);
        }
    } else {
        float blendStart = shadowData.nearSplits[cascadeLevel + 1];
        float blendEnd = shadowData.farSplits[cascadeLevel];

        if (viewSpaceDepth >= blendStart) {
            float4 nextLightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel + 1], float4(worldPos, 1.0));
            float3 nextProjCoords = nextLightSpacePos.xyz / nextLightSpacePos.w;
            nextProjCoords.xy = nextProjCoords.xy * 0.5 + 0.5;

            uint nextShadowMapIndex = csmIndices[cascadeLevel + 1];
            Texture2D nextShadowMap = textures[NonUniformResourceIndex(nextShadowMapIndex)];

            float nextLightSize = shadowData->lightSizes[cascadeLevel + 1];
            int nextPcfSamples = int(shadowData->pcfSamples[cascadeLevel + 1]);

            float shadowFactor2 = PCF(nextShadowMap, comparisonSampler, nextProjCoords.xy, nextProjCoords.z, nextLightSize, nextPcfSamples);
            // float shadowFactor2 = PCSS(nextShadowMap, sampler, comparisonSampler, nextProjCoords.xy, nextProjCoords.z, viewSpaceDepth, nextLightSize, shadowData->nearSplits[cascadeLevel + 1], shadowData->farSplits[cascadeLevel + 1], nextPcfSamples);

            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            outShadowFactor = lerp(shadowFactor1, shadowFactor2, blendFactor);
        }
    }

     return lerp(shadowData.shadowIntensity, 1.0, outShadowFactor);
}