import model_interop;
import flags_interop;
import shader_functions;
import bindless_declarations;

public static const float RT_ALPHA_TEST_TEXTURE_LOD = 0.0;

public struct RTAlphaTestBuffers
{
    public Instance* instances;
    public Primitive* primitives;
    public MaterialProperties* materials;
    public uint32_t* indices;
    public VertexAttribute* vertexAttrs;
};

/** Alpha test for a candidate/committed triangle on a cutout instance. Non-cutout surfaces always pass. */
public bool RTAlphaTestPasses(RTAlphaTestBuffers b, uint instanceID, uint triangleIndex, float2 barycentrics)
{
    Instance instance = b.instances[instanceID];
    MaterialProperties material = b.materials[instance.materialIndex];
    if ((instance.flags & INSTANCE_FLAG_ALPHA_CUTOUT) == 0u || uint(material.alphaProperties.y) != MATERIAL_ALPHA_CUTOUT) {
        return true;
    }

    Primitive primitive = b.primitives[instance.primitiveIndex];
    uint firstIndex = primitive.indexOffset + triangleIndex * 3;
    VertexAttribute a0 = b.vertexAttrs[b.indices[firstIndex + 0]];
    VertexAttribute a1 = b.vertexAttrs[b.indices[firstIndex + 1]];
    VertexAttribute a2 = b.vertexAttrs[b.indices[firstIndex + 2]];
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

/** Runs traversal to completion, committing non-opaque candidates that pass the alpha test. Trace with RAY_FLAG_FORCE_OPAQUE as a runtime flag to bypass. */
public void RTProceedAlphaTested<let FLAGS : uint>(inout RayQuery<FLAGS> q, RTAlphaTestBuffers b)
{
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && RTAlphaTestPasses(b, q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())) {
            q.CommitNonOpaqueTriangleHit();
        }
    }
}
