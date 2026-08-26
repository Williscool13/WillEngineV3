import model_interop;
import flags_interop;
import shader_functions;
import bindless_declarations;

public static const float RT_ALPHA_TEST_TEXTURE_LOD = 0.0;

/** Alpha test for a candidate/committed triangle on a cutout instance. Non-cutout surfaces always pass. */
public bool RTAlphaTestPasses(Instance* instanceBuffer, Primitive* primitiveBuffer, MaterialProperties* materialBuffer, uint32_t* indexBuffer, VertexAttribute* vertexAttrBuffer,
                              uint instanceID, uint triangleIndex, float2 barycentrics)
{
    Instance instance = instanceBuffer[instanceID];
    MaterialProperties material = materialBuffer[instance.materialIndex];
    if ((instance.flags & INSTANCE_FLAG_ALPHA_CUTOUT) == 0u || uint(material.alphaProperties.y) != MATERIAL_ALPHA_CUTOUT) {
        return true;
    }

    Primitive primitive = primitiveBuffer[instance.primitiveIndex];
    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = vertexAttrBuffer[indexBuffer[firstIndex + 0]];
    VertexAttribute a1 = vertexAttrBuffer[indexBuffer[firstIndex + 1]];
    VertexAttribute a2 = vertexAttrBuffer[indexBuffer[firstIndex + 2]];
    float3 bary = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

    float2 uv = bary.x * DecodeUV(a0) + bary.y * DecodeUV(a1) + bary.z * DecodeUV(a2);
    uv = uv * material.colorUvTransform.xy + material.colorUvTransform.zw;
    float alpha = material.colorFactor.a * (bary.x * DecodeColor(a0).a + bary.y * DecodeColor(a1).a + bary.z * DecodeColor(a2).a);
    if (material.textureImageIndices.x >= 0) {
        uint samplerIndex = material.textureSamplerIndices.x >= 0 ? uint(material.textureSamplerIndices.x) : 0u;
        alpha *= assetTextures[NonUniformResourceIndex(uint(material.textureImageIndices.x))].SampleLevel(assetSamplers[NonUniformResourceIndex(samplerIndex)], uv, RT_ALPHA_TEST_TEXTURE_LOD).a;
    }
    return alpha >= material.alphaProperties.x;
}
