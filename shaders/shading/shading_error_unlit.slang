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

    float2 uv = shadingInputs.interp.uv;
    float4 albedo = assetTextures[ERROR_IMAGE_BINDLESS_INDEX].SampleGrad(assetSamplers[ASSET_SAMPLER_NEAREST_BINDLESS_INDEX], uv, shadingInputs.uvDdx, shadingInputs.uvDdy);

    rdgStorageUInt4[pc.gbufferOneIndex][pixel] = uint4(
        PackNormalOct16(shadingInputs.interp.normalVS),
        PackMotionVectors(shadingInputs.velocity),
        PackPBR(1.0, 0.0),
        1u
    );
    rdgStorageUInt2[pc.gbufferTwoIndex][pixel] = uint2(PackAlbedoRGB8(albedo), PackEmissiveRGBE(float3(0.0)));
}
