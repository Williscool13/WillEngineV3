import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;

import limits_declarations;
import bindless_declarations;

import shader_functions;

[shader("compute")]
[numthreads(16, 16, 1)]
void VisibilityShading(uint3 dtid : SV_DispatchThreadID, uniform VisibilityShadingPushConstant pc)
{
    if (dtid.x >= pc.extents.x || dtid.y >= pc.extents.y) { return; }

    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;
    if (vis.x == 0 && vis.y == 0) { return; }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];
    MaterialProperties mat = pc.materialBuffer[inst.materialIndex];

    Meshlet meshlet = pc.meshletBuffer[meshletIndex];
    uint triangleIndex = meshlet.meshletTriangleOffset + triangleIndexWithinMeshlet;
    uint packedTriangle = pc.meshletTrianglesBuffer[triangleIndex];
    uint idx0 = (packedTriangle >> 0) & 0xFF;
    uint idx1 = (packedTriangle >> 8) & 0xFF;
    uint idx2 = (packedTriangle >> 16) & 0xFF;
    uint v0i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx0];
    uint v1i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx1];
    uint v2i = pc.meshletVerticesBuffer[meshlet.meshletVertexOffset + idx2];
    VertexPosition v0 = pc.vertexPosBuffer[meshlet.vertexOffset + v0i];
    VertexPosition v1 = pc.vertexPosBuffer[meshlet.vertexOffset + v1i];
    VertexPosition v2 = pc.vertexPosBuffer[meshlet.vertexOffset + v2i];

    VertexAttribute va0 = pc.vertexAttrBuffer[meshlet.vertexOffset + v0i];
    VertexAttribute va1 = pc.vertexAttrBuffer[meshlet.vertexOffset + v1i];
    VertexAttribute va2 = pc.vertexAttrBuffer[meshlet.vertexOffset + v2i];

    float2 bary2 = rdgStorageFloat2[pc.barycentricBufferIndex].Load(dtid.xy);
    float3 bary = float3(bary2.x, bary2.y, 1.0 - bary2.x - bary2.y);
    float4 derivs = rdgStorageFloat4[pc.derivativeBufferIndex].Load(dtid.xy);
    float2 uvDdx = derivs.xy;
    float2 uvDdy = derivs.zw;

    float4x4 mv = mul(pc.sceneData->view, m.modelMatrix);
    float3 extent = prim.boundingBoxMax - prim.boundingBoxMin;
    float3 objPos = bary.x * DecodePosition(v0, prim.boundingBoxMin, extent)
                  + bary.y * DecodePosition(v1, prim.boundingBoxMin, extent)
                  + bary.z * DecodePosition(v2, prim.boundingBoxMin, extent);
    float3 pos = mul(mv, float4(objPos, 1.0)).xyz;
    InterpolatedVertex interp = InterpolateVertex(bary, va0, va1, va2, mv);

    // TODO: normal maps not yet implemented
    // float3 objTangent = bary.x * v0.tangent.xyz + bary.y * v1.tangent.xyz + bary.z * v2.tangent.xyz;
    // float tangentSign = bary.x * v0.tangent.w + bary.y * v1.tangent.w + bary.z * v2.tangent.w;
    // float3 tangentVS = normalize(mul((float3x3)mv, objTangent));
    // tangentVS = normalize(tangentVS - dot(tangentVS, interp.normalVS) * interp.normalVS);
    // float3 bitangentVS = cross(interp.normalVS, tangentVS) * tangentSign;

    // Albedo
    float2 colorUV = interp.uv * mat.colorUvTransform.xy + mat.colorUvTransform.zw;
    float4 albedo = mat.colorFactor * interp.color;
    if (mat.textureImageIndices.x >= 0) {
        float2 ddx = uvDdx * mat.colorUvTransform.xy;
        float2 ddy = uvDdy * mat.colorUvTransform.xy;
        albedo *= assetTextures[NonUniformResourceIndex(mat.textureImageIndices.x)].SampleGrad(assetSamplers[NonUniformResourceIndex(mat.textureSamplerIndices.x)], colorUV, ddx, ddy);
    }

    // TODO: metallic/roughness texture not yet implemented
    // float2 metalRoughUV = interp.uv * mat.metalRoughUvTransform.xy + mat.metalRoughUvTransform.zw;
    float metallic  = mat.metalRoughFactors.x;
    float roughness = mat.metalRoughFactors.y;
    // if (mat.textureImageIndices.y >= 0) {
    //     float2 ddx = uvDdx * mat.metalRoughUvTransform.xy;
    //     float2 ddy = uvDdy * mat.metalRoughUvTransform.xy;
    //     float4 mr = assetTextures[mat.textureImageIndices.y].SampleGrad(assetSamplers[mat.textureSamplerIndices.y], metalRoughUV, ddx, ddy);
    //     metallic  *= mr.b;
    //     roughness *= mr.g;
    // }

    // TODO: normal maps not yet implemented
    float3 shadingNormal = interp.normalVS;
    // if (mat.textureImageIndices.z >= 0) {
    //     float2 normalUV = interp.uv * mat.normalUvTransform.xy + mat.normalUvTransform.zw;
    //     float2 ddx = uvDdx * mat.normalUvTransform.xy;
    //     float2 ddy = uvDdy * mat.normalUvTransform.xy;
    //     float3 tsNormal = assetTextures[mat.textureImageIndices.z].SampleGrad(assetSamplers[mat.textureSamplerIndices.z], normalUV, ddx, ddy).xyz * 2.0 - 1.0;
    //     shadingNormal = normalize(tsNormal.x * tangentVS + tsNormal.y * bitangentVS + tsNormal.z * interp.normalVS);
    // }

    // Emissive
    float3 emissiveColor = mat.emissiveFactor.rgb;
    if (mat.textureImageIndices.w >= 0) {
        float2 emissiveUV = interp.uv * mat.emissiveUvTransform.xy + mat.emissiveUvTransform.zw;
        float2 ddx = uvDdx * mat.emissiveUvTransform.xy;
        float2 ddy = uvDdy * mat.emissiveUvTransform.xy;
        emissiveColor *= assetTextures[mat.textureImageIndices.w].SampleGrad(assetSamplers[mat.textureSamplerIndices.w], emissiveUV, ddx, ddy).rgb;
    }

    float4 currClip = mul(pc.sceneData->viewProj, mul(m.modelMatrix, float4(objPos, 1.0)));
    float4 prevClip = mul(pc.sceneData->prevViewProj, mul(m.prevModelMatrix, float4(objPos, 1.0)));
    float2 currNDC = currClip.xy / currClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    float2 velocity = (currNDC - pc.sceneData->jitter) - (prevNDC - pc.sceneData->prevJitter);

    uint4 gbufferOne = uint4(
        PackNormalOct16(shadingNormal),
        PackMotionVectors(velocity),
        PackPBR(roughness, metallic),
        0u
    );
    rdgStorageUInt4[pc.gbufferOneIndex][dtid.xy] = gbufferOne;
    rdgStorageUInt2[pc.gbufferTwoIndex][dtid.xy] = uint2(PackAlbedoRGB8(albedo), PackEmissiveRGBE(emissiveColor));
}
