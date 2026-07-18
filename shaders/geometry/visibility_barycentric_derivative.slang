import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;

import shader_constants;
import shader_functions;
import bindless_declarations;

// ==================== Barycentric Derivatives ====================
struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

// Perspective-correct barycentrics + analytic UV derivatives.
// From: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
// pt0/pt1/pt2: clip-space positions; pixelNdc: NDC of current pixel (Y-up convention);
// winSize: viewport dimensions in pixels.
BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
    BarycentricDeriv ret = (BarycentricDeriv)0;

    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
    float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f    + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f    + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    ret.m_ddx *= (2.0f / winSize.x);
    ret.m_ddy *= (2.0f / winSize.y);
    ddxSum    *= (2.0f / winSize.x);
    ddySum    *= (2.0f / winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum    *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}


// ==================== Compute Shader ====================
[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeVisibilityBarycentricDerivative(uint3 dtid : SV_DispatchThreadID, uniform VisibilityBufferResolvePushConstant pc)
{
    if (dtid.x >= pc.extents.x || dtid.y >= pc.extents.y) {
        return;
    }

    uint2 vis = rdgTexturesUInt2[pc.visibilityBufferIndex].Load(int3(dtid.xy, 0)).xy;

    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
        return;
    }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    Instance inst = pc.instanceBuffer[instanceIndex];
    Primitive prim = pc.primitiveBuffer[inst.primitiveIndex];
    Model m = pc.modelBuffer[inst.modelIndex];

    float3 extent = prim.boundingBoxMax - prim.boundingBoxMin;

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
    float3 v0pos = DecodePosition(v0, prim.boundingBoxMin, extent);
    VertexPosition v1 = pc.vertexPosBuffer[meshlet.vertexOffset + v1i];
    float3 v1pos = DecodePosition(v1, prim.boundingBoxMin, extent);
    VertexPosition v2 = pc.vertexPosBuffer[meshlet.vertexOffset + v2i];
    float3 v2pos = DecodePosition(v2, prim.boundingBoxMin, extent);

    float4x4 mvp = mul(pc.sceneData->viewProj, m.modelMatrix);
    float4 p0 = mul(mvp, float4(v0pos, 1.0));
    float4 p1 = mul(mvp, float4(v1pos, 1.0));
    float4 p2 = mul(mvp, float4(v2pos, 1.0));

    float2 pixelNDC = (float2(dtid.xy) + 0.5) / float2(pc.extents) * 2.0 - 1.0;

    BarycentricDeriv baryDeriv = CalcFullBary(p0, p1, p2, pixelNDC, float2(pc.extents));
    float3 bary = baryDeriv.m_lambda;

    VertexAttribute va0 = pc.vertexAttrBuffer[meshlet.vertexOffset + v0i];
    float2 uv0 = DecodeUV(va0);
    VertexAttribute va1 = pc.vertexAttrBuffer[meshlet.vertexOffset + v1i];
    float2 uv1 = DecodeUV(va1);
    VertexAttribute va2 = pc.vertexAttrBuffer[meshlet.vertexOffset + v2i];
    float2 uv2 = DecodeUV(va2);

    float2 uvDdx = baryDeriv.m_ddx.x * uv0 + baryDeriv.m_ddx.y * uv1 + baryDeriv.m_ddx.z * uv2;
    float2 uvDdy = baryDeriv.m_ddy.x * uv0 + baryDeriv.m_ddy.y * uv1 + baryDeriv.m_ddy.z * uv2;

    rdgStorageFloat2[pc.barycentricTargetIndex][dtid.xy] = float2(bary.x, bary.y);
    rdgStorageFloat4[pc.derivativeTargetIndex][dtid.xy] = float4(uvDdx, uvDdy);

    // Note: We could interpolate here to avoid another vertex fetch in the subsequent pass.
    //  But this shader will be large, and the barycentrics become useless.  I'm sure there are other good reasons put forward by Doom TDA
}
