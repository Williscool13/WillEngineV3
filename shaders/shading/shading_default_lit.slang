import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import limits_declarations;
import bindless_declarations;
import shader_functions;
import visibility_shading_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeMain(uint3 dtid : SV_DispatchThreadID, uniform VisibilityShadingPushConstant pc)
{
    ShadeDispatchParameters shadeParams = pc.shadeDispatchBuffer[pc.materialIndex];
    uint2 pixel = dtid.xy + uint2(shadeParams.minX, shadeParams.minY);
    uint2 boundsMax = uint2(shadeParams.maxX, shadeParams.maxY);
    if (any(pixel >= boundsMax)) {
        return;
    }

    ReconstructedShadingData data = ReconstructShadingInputs(
        pixel,
        pc.visibilityBufferIndex, pc.barycentricBufferIndex, pc.derivativeBufferIndex,
        pc.materialIndex,
        pc.instanceBuffer, pc.primitiveBuffer, pc.modelBuffer,
        pc.meshletBuffer, pc.meshletVerticesBuffer, pc.meshletTrianglesBuffer,
        pc.vertexPosBuffer, pc.vertexAttrBuffer,
        pc.sceneData
    );
    if (!data.valid) {
        return;
    }
    ShadingInputs shadingInputs = data.shadingInputs;

    MaterialProperties material = pc.materialBuffer[pc.materialIndex];

    float3 shadingNormal = shadingInputs.interp.normalVS;

    // Albedo
    float2 colorUV = shadingInputs.interp.uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
    float4 albedo = material.colorFactor * shadingInputs.interp.color;
    if (material.textureImageIndices.x >= 0) {
        float2 ddx = shadingInputs.uvDdx * material.colorUvTransform.xy;
        float2 ddy = shadingInputs.uvDdy * material.colorUvTransform.xy;
        albedo *= assetTextures[material.textureImageIndices.x].SampleGrad(assetSamplers[material.textureSamplerIndices.x], colorUV, ddx, ddy);
    }

    float metallic  = material.metalRoughFactors.x;
    float roughness = material.metalRoughFactors.y;
    // TODO: metallic/roughness texture sampling

    // TODO: normal map sampling

    // Emissive
    float3 emissiveColor = material.emissiveFactor.rgb;
    if (material.textureImageIndices.w >= 0) {
        float2 emissiveUV = shadingInputs.interp.uv * material.emissiveUvTransform.xy + material.emissiveUvTransform.zw;
        float2 ddx = shadingInputs.uvDdx * material.emissiveUvTransform.xy;
        float2 ddy = shadingInputs.uvDdy * material.emissiveUvTransform.xy;
        emissiveColor *= assetTextures[material.textureImageIndices.w].SampleGrad(assetSamplers[material.textureSamplerIndices.w], emissiveUV, ddx, ddy).rgb;
    }

    uint4 gbufferOne = uint4(
        PackNormalOct16(shadingNormal),
        PackMotionVectors(shadingInputs.velocity),
        PackPBR(roughness, metallic),
        0u
    );
    rdgStorageUInt4[pc.gbufferOneIndex][pixel] = gbufferOne;
    rdgStorageUInt2[pc.gbufferTwoIndex][pixel] = uint2(PackAlbedoRGB8(albedo), PackEmissiveRGBE(emissiveColor));
}
