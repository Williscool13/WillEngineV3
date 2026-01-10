module shadow_functions;

import constants_interop;
import shadows_interop;

public uint getCascadeIndex(uint packedIndices, uint cascadeLevel)
{
    return (packedIndices >> (cascadeLevel * 8)) & 0xFF;
}

public float getShadowFactor(float3 worldPos, float viewSpaceDepth, ShadowData* shadowData, uint32_t packedCSMIndices,
                            Texture2D textures[], SamplerState pointSampler, SamplerComparisonState comparisonSampler) {
    if (viewSpaceDepth < shadowData->nearSplits[0]) {
        return 0.0f; // Too close = shadowed
    }
    if (viewSpaceDepth > shadowData.farSplits[SHADOW_CASCADE_COUNT - 1]) {
        return 1.0f; // Too far = unshadowed
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

    float shadowFactor1 = shadowMap.SampleCmpLevelZero(comparisonSampler, projCoords.xy, projCoords.z);

    // Cascade blending
    if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
        // Last cascade - blend to unshadowed
        float blendStart = shadowData.farSplits[cascadeLevel] * 0.9;
        float blendEnd = shadowData.farSplits[cascadeLevel];
        if (viewSpaceDepth >= blendStart) {
            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);
            return lerp(shadowFactor1, 1.0, blendFactor);
        }
    } else {
        // Blend with next cascade
        float blendStart = shadowData.nearSplits[cascadeLevel + 1];
        float blendEnd = shadowData.farSplits[cascadeLevel];

        if (viewSpaceDepth >= blendStart) {
            // Sample next cascade
            float4 nextLightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel + 1], float4(worldPos, 1.0));
            float3 nextProjCoords = nextLightSpacePos.xyz / nextLightSpacePos.w;
            nextProjCoords.xy = nextProjCoords.xy * 0.5 + 0.5;
            nextProjCoords.y = 1 - nextProjCoords.y;

            uint nextShadowMapIndex = getCascadeIndex(packedCSMIndices, cascadeLevel + 1);
            Texture2D nextShadowMap = textures[NonUniformResourceIndex(nextShadowMapIndex)];

            float shadowFactor2 = nextShadowMap.SampleCmpLevelZero(comparisonSampler, nextProjCoords.xy, nextProjCoords.z);
            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);

            return lerp(shadowFactor1, shadowFactor2, blendFactor);
        }
    }

    return shadowFactor1;
}