import common_interop;
import constants_interop;
import flags_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import limits_declarations;
import bindless_declarations;
import shader_functions;
import visibility_shading_common;
import bucket_dispatch_common;

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeShadingDefaultLit(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uniform VisibilityShadingPushConstant pc)
{
    uint2 pixel = BucketPixel(pc.tileListBuffer, pc.tileCapacity, pc.materialIndex, gid.x, gtid.xy);
    if (any(pixel >= pc.extents)) {
        return;
    }

    ReconstructedShadingData data = ReconstructShadingInputs(
        pixel, pc.extents,
        pc.visibilityBufferIndex,
        pc.materialIndex,
        pc.materialBuffer[pc.materialIndex].alphaProperties.z != 0.0,
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
    float3 T = shadingInputs.interp.tangentVS;
    float3 N = shadingNormal;
    float3 B = cross(N, T) * shadingInputs.interp.tangentSign;

    SurfaceUVLayers layers = BuildSurfaceUVLayers(material, shadingInputs, data.model.modelMatrix, N, T, B);

    // Albedo
    float4 albedo = material.colorFactor * shadingInputs.interp.color;
    if (material.textureImageIndices.x >= 0) {
        albedo *= SampleSurfaceLayers(layers, material.textureImageIndices.x, material.textureSamplerIndices.x, material.colorUvTransform);
    }

    float metallic  = material.metalRoughFactors.x;
    float roughness = material.metalRoughFactors.y;
    if (material.textureImageIndices.y >= 0) {
        // glTF packs roughness in G and metalness in B.
        float4 mr = SampleSurfaceLayers(layers, material.textureImageIndices.y, material.textureSamplerIndices.y, material.metalRoughUvTransform);
        roughness *= mr.g;
        metallic *= mr.b;
    }

    // Normal map
    if (material.textureImageIndices.z >= 0) {
        shadingNormal = SampleSurfaceLayersNormal(layers, material.textureImageIndices.z, material.textureSamplerIndices.z, material.normalUvTransform, material.physicalProperties.z, N);
    }

    // Emissive
    float3 emissiveColor = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureImageIndices.w >= 0) {
        emissiveColor *= SampleSurfaceLayers(layers, material.textureImageIndices.w, material.textureSamplerIndices.w, material.emissiveUvTransform).rgb;
    }

    uint4 gbufferOne = uint4(
        PackNormalOct16(shadingNormal),
        PackMotionVectors(shadingInputs.velocity),
        PackPBR(roughness, metallic),
        GBUFFER1_LIT_BIT | (any(emissiveColor > float3(0.0)) ? GBUFFER1_EMISSIVE_BIT : 0u) | ((data.instance.flags & INSTANCE_FLAG_MOTION_BLUR) == 0u ? GBUFFER1_NO_MOTION_BLUR_BIT : 0u)
        | ((data.instance.flags & INSTANCE_FLAG_CAMERA_MOTION_BLUR) == 0u ? GBUFFER1_NO_CAMERA_MOTION_BLUR_BIT : 0u) | PackViewZDelta(shadingInputs.viewZDelta)
    );
    rdgStorageUInt4[pc.gbufferOneIndex][pixel] = gbufferOne;
    rdgStorageUInt2[pc.gbufferTwoIndex][pixel] = uint2(PackAlbedoRGBA8(albedo), PackEmissiveRGBE(emissiveColor));
    rdgStorageFloat4[pc.shadowOriginOffsetIndex][pixel] = float4(data.shadowOriginOffsetWS, 0.0);
}
