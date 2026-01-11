module shadow_functions;

import common_interop;
import constants_interop;
import shadows_interop;
import shader_constants;

public uint getCascadeIndex(uint packedIndices, uint cascadeLevel)
{
    return (packedIndices >> (cascadeLevel * 8)) & 0xFF;
}

float pcf(Texture2D shadowMap, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float filterRadius, int32_t sampleCount) {
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

float findBlockerDistance(Texture2D shadowMap, SamplerState sampler, float2 uv, float receiverDepth, float searchRadius, int numSamples)
{
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < numSamples; i++) {
        float2 offset = POISSON_DISK_64[i] * searchRadius;
        float shadowDepth = shadowMap.SampleLevel(sampler, uv + offset, 0).r;

        if (shadowDepth >= receiverDepth) {
            blockerSum += shadowDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0) {
        return -1.0;
    }

    return blockerSum / float(blockerCount);
}

float pcfFilter(Texture2D shadowMap, SamplerComparisonState comparisonSampler, float2 uv, float receiverDepth, float filterRadius, int numSamples)
{
    float shadow = 0.0;

    for (int i = 0; i < numSamples; i++) {
        float2 offset = POISSON_DISK_64[i] * filterRadius;
        shadow += shadowMap.SampleCmpLevelZero(comparisonSampler, uv + offset, receiverDepth);
    }

    return shadow / float(numSamples);
}

float pcss(Texture2D shadowMap, SamplerState sampler, SamplerComparisonState comparisonSampler,
           float2 uv, float receiverDepth, float lightSize, float nearPlane, float farPlane, int blockerSamples, int pcfSamples)
{
    // Blocker search
    float searchRadius = lightSize;
    // float searchRadius = lightSize * (receiverDepth - nearPlane) / receiverDepth;
    float avgBlockerDepth = findBlockerDistance(shadowMap, sampler, uv, receiverDepth, searchRadius, blockerSamples);

    if (avgBlockerDepth < 0.0) {
        return 1.0f;
    }

    // Penumbra estimation
    // float penumbra = (avgBlockerDepth - receiverDepth) / avgBlockerDepth;
    // float penumbra = (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
    float penumbra = (avgBlockerDepth - receiverDepth) * lightSize;
    // float filterRadius = penumbra * lightSize * nearPlane / receiverDepth;
    float filterRadius = penumbra;
    filterRadius = clamp(filterRadius, 0.001, 0.05);


    // PCF filtering
    return pcfFilter(shadowMap, comparisonSampler, uv, receiverDepth, filterRadius, pcfSamples);
}

/*public float getShadowFactor(float3 worldPos, float viewSpaceDepth, ShadowData* shadowData, uint32_t packedCSMIndices,
                            Texture2D textures[], SamplerState sampler, SamplerComparisonState comparisonSampler)
{
    if (viewSpaceDepth < shadowData->nearSplits[0]) {
        return 0.0f;
    }
    if (viewSpaceDepth > shadowData.farSplits[SHADOW_CASCADE_COUNT - 1]) {
        return 1.0f;
    }

    int cascadeLevel = SHADOW_CASCADE_COUNT - 1;
    for (int i = 1; i < SHADOW_CASCADE_COUNT - 1; ++i) {
        if (viewSpaceDepth < shadowData.nearSplits[i]) {
            cascadeLevel = i - 1;
            break;
        }
    }

    float4 lightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y = 1 - projCoords.y;

    uint shadowMapIndex = getCascadeIndex(packedCSMIndices, cascadeLevel);
    Texture2D shadowMap = textures[NonUniformResourceIndex(shadowMapIndex)];

    float lightSize = shadowData->lightSizes[cascadeLevel];
    int blockerSamples = int(shadowData->blockerSearchSamples[cascadeLevel]);
    int pcfSamples = int(shadowData->pcfSamples[cascadeLevel]);

    float shadowFactor1 = pcss(shadowMap, sampler, comparisonSampler, projCoords.xy, projCoords.z,
                               lightSize, shadowData->nearSplits[cascadeLevel], shadowData->farSplits[cascadeLevel], blockerSamples, pcfSamples);

    // Cascade blending
    if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
        float blendStart = shadowData.farSplits[cascadeLevel] * 0.9;
        float blendEnd = shadowData.farSplits[cascadeLevel];
        if (viewSpaceDepth >= blendStart) {
            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            return lerp(shadowFactor1, 1.0, blendFactor);
        }
    } else {
        float blendStart = shadowData.nearSplits[cascadeLevel + 1];
        float blendEnd = shadowData.farSplits[cascadeLevel];

        if (viewSpaceDepth >= blendStart) {
            float4 nextLightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel + 1], float4(worldPos, 1.0));
            float3 nextProjCoords = nextLightSpacePos.xyz / nextLightSpacePos.w;
            nextProjCoords.xy = nextProjCoords.xy * 0.5 + 0.5;
            nextProjCoords.y = 1 - nextProjCoords.y;

            uint nextShadowMapIndex = getCascadeIndex(packedCSMIndices, cascadeLevel + 1);
            Texture2D nextShadowMap = textures[NonUniformResourceIndex(nextShadowMapIndex)];

            float nextLightSize = shadowData->lightSizes[cascadeLevel + 1];
            int nextBlockerSamples = int(shadowData->blockerSearchSamples[cascadeLevel + 1]);
            int nextPcfSamples = int(shadowData->pcfSamples[cascadeLevel + 1]);

            float shadowFactor2 = pcss(nextShadowMap, sampler, comparisonSampler, nextProjCoords.xy, nextProjCoords.z,
                                      nextLightSize, shadowData->nearSplits[cascadeLevel + 1], shadowData->farSplits[cascadeLevel + 1], nextBlockerSamples, nextPcfSamples);

            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            return lerp(shadowFactor1, shadowFactor2, blendFactor);
        }
    }

    return shadowFactor1;
}*/

public float getShadowFactor(float3 viewPos, float3 viewNormal, SceneData* sceneData, ShadowData* shadowData, uint32_t packedCSMIndices,
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
    // float offset = 0.05f;
    // float3 offsetPosition = viewPos + viewNormal * offset;
    float4 offsetWorldPosition = mul(sceneData.invView, float4(offsetPosition, 1.0f));
    float3 worldPos = offsetWorldPosition.xyz;

    float4 lightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y = 1 - projCoords.y;

    uint shadowMapIndex = getCascadeIndex(packedCSMIndices, cascadeLevel);
    Texture2D shadowMap = textures[NonUniformResourceIndex(shadowMapIndex)];

    float lightSize = shadowData->lightSizes[cascadeLevel];
    int pcfSamples = int(shadowData->pcfSamples[cascadeLevel]);

    float shadowFactor1 = pcf(shadowMap, comparisonSampler, projCoords.xy, projCoords.z, lightSize, pcfSamples);

    // Cascade blending
    if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
        // float blendStart = shadowData.farSplits[cascadeLevel] * 0.9;
        // float blendEnd = shadowData.farSplits[cascadeLevel];
        // if (viewSpaceDepth >= blendStart) {
        //     float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
        //     return lerp(shadowFactor1, 1.0, blendFactor);
        // }
    } else {
        float blendStart = shadowData.nearSplits[cascadeLevel + 1];
        float blendEnd = shadowData.farSplits[cascadeLevel];

        if (viewSpaceDepth >= blendStart) {
            float4 nextLightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel + 1], float4(worldPos, 1.0));
            float3 nextProjCoords = nextLightSpacePos.xyz / nextLightSpacePos.w;
            nextProjCoords.xy = nextProjCoords.xy * 0.5 + 0.5;
            nextProjCoords.y = 1 - nextProjCoords.y;

            uint nextShadowMapIndex = getCascadeIndex(packedCSMIndices, cascadeLevel + 1);
            Texture2D nextShadowMap = textures[NonUniformResourceIndex(nextShadowMapIndex)];

            float nextLightSize = shadowData->lightSizes[cascadeLevel + 1];
            int nextPcfSamples = int(shadowData->pcfSamples[cascadeLevel + 1]);

            float shadowFactor2 = pcf(nextShadowMap, comparisonSampler, nextProjCoords.xy, nextProjCoords.z, nextLightSize, nextPcfSamples);

            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            return lerp(shadowFactor1, shadowFactor2, blendFactor);
        }
    }

    return shadowFactor1;
}