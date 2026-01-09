module shadow_functions;

import constants_interop;
import shadows_interop;

public uint GetCascadeIndex(uint packedIndices, uint cascadeLevel)
{
    return (packedIndices >> (cascadeLevel * 8)) & 0xFF;
}

public float getShadowFactor(float3 worldPos, float viewSpaceDepth, ShadowData* shadowData, uint32_t packedCSMIndices, Texture2D textures[], SamplerState sampler) {
    if (viewSpaceDepth < shadowData->nearSplits[0]) {
        return 0.0f; // Too close = shadowed
    }
    if (viewSpaceDepth > shadowData.farSplits[SHADOW_CASCADE_COUNT - 1]) {
        return 1.0f; // Too far = unshadowed
    }

    int cascadeLevel = SHADOW_CASCADE_COUNT - 1;
    for (int i = 0; i < SHADOW_CASCADE_COUNT - 1; ++i) {
        if (viewSpaceDepth <= shadowData.farSplits[i]) {
            cascadeLevel = i;
            break;
        }
    }
     cascadeLevel = 0;


    float4 lightSpacePos = mul(shadowData.lightSpaceMatrices[cascadeLevel], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.y = 1 - projCoords.y;


    uint shadowMapIndex = GetCascadeIndex(packedCSMIndices, cascadeLevel);
    Texture2D shadowMap = textures[shadowMapIndex];

    float shadowDepth = shadowMap.SampleLevel(sampler, projCoords.xy, 0).r;
    float shadowFactor1 = projCoords.z >= shadowDepth ? 1.0f : 0.0f;

    // Cascade blending
    /*if (cascadeLevel == SHADOW_CASCADE_COUNT - 1) {
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

            uint nextShadowMapIndex = GetCascadeIndex(packedCSMIndices, cascadeLevel + 1);
            Texture2D nextShadowMap = textures[nextShadowMapIndex];


            float nextShadowDepth = nextShadowMap.SampleLevel(sampler, nextProjCoords.xy, 0).r;
            // float shadowFactor2 = nextProjCoords.z <= nextShadowDepth ? 1.0f : 0.0f;
            float shadowFactor2 = nextProjCoords.z >= nextShadowDepth ? 1.0f : 0.0f;
            float blendFactor = smoothstep(blendStart, blendEnd, viewSpaceDepth);

            return lerp(shadowFactor1, shadowFactor2, blendFactor);
        }
    }*/

    return shadowFactor1;
}