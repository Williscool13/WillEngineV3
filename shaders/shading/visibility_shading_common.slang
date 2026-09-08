module visibility_shading_common;

import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;

public struct ShadingInputs
{
    public float3 objPos;
    public float3 posVS;
    public InterpolatedVertex interp;
    public float2 uvDdx;
    public float2 uvDdy;
    public float2 velocity;
    public float viewZDelta;
    public float4x4 mv;
}

public struct ReconstructedShadingData
{
    public bool valid;
    public ShadingInputs shadingInputs;
    public Instance instance;
    public Primitive primitive;
    public Model model;
}

struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

// Perspective-correct barycentrics + analytic screen-space derivatives.
// From: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
static BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
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
    ret.m_lambda.y = interpW * (0.0f + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    ret.m_ddx *= (2.0f / winSize.x);
    ret.m_ddy *= (2.0f / winSize.y);
    ddxSum *= (2.0f / winSize.x);
    ddySum *= (2.0f / winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}

public ReconstructedShadingData ReconstructShadingInputs(
    uint2 pixel,
    uint2 extents,
    uint32_t visibilityBufferIndex,
    uint32_t materialIndex,
    Instance* instanceBuffer,
    Primitive* primitiveBuffer,
    Model* modelBuffer,
    Meshlet* meshletBuffer,
    uint32_t* meshletVerticesBuffer,
    uint32_t* meshletTrianglesBuffer,
    VertexPosition* vertexPositionBuffer,
    VertexAttribute* vertexAttributeBuffer,
    SceneData* sceneData
)
{
    ReconstructedShadingData result;
    result.valid = false;

    uint2 vis = rdgTexturesUInt2[visibilityBufferIndex].Load(int3(pixel, 0)).xy;
    if (vis.x == VISIBILITY_BUFFER_SENTINEL && vis.y == VISIBILITY_BUFFER_SENTINEL) {
        return result;
    }

    uint meshletIndex = vis.x >> 8;
    uint triangleIndexWithinMeshlet = vis.x & 0xFF;
    uint instanceIndex = vis.y;

    result.instance = instanceBuffer[instanceIndex];
    if (result.instance.materialIndex != materialIndex) {
        return result;
    }

    result.primitive = primitiveBuffer[result.instance.primitiveIndex];
    result.model = modelBuffer[result.instance.modelIndex];

    Meshlet meshlet = meshletBuffer[meshletIndex];
    uint triangleIndex = meshlet.meshletTriangleOffset + triangleIndexWithinMeshlet;
    uint packedTriangle = meshletTrianglesBuffer[triangleIndex];
    uint idx0 = (packedTriangle >> 0) & 0xFF;
    uint idx1 = (packedTriangle >> 8) & 0xFF;
    uint idx2 = (packedTriangle >> 16) & 0xFF;
    uint v0i = meshletVerticesBuffer[meshlet.meshletVertexOffset + idx0];
    uint v1i = meshletVerticesBuffer[meshlet.meshletVertexOffset + idx1];
    uint v2i = meshletVerticesBuffer[meshlet.meshletVertexOffset + idx2];

    VertexPosition vertexPosition0 = vertexPositionBuffer[meshlet.vertexOffset + v0i];
    VertexPosition vertexPosition1 = vertexPositionBuffer[meshlet.vertexOffset + v1i];
    VertexPosition vertexPosition2 = vertexPositionBuffer[meshlet.vertexOffset + v2i];
    VertexAttribute vertexAttribute0 = vertexAttributeBuffer[meshlet.vertexOffset + v0i];
    VertexAttribute vertexAttribute1 = vertexAttributeBuffer[meshlet.vertexOffset + v1i];
    VertexAttribute vertexAttribute2 = vertexAttributeBuffer[meshlet.vertexOffset + v2i];

    float3 extent = result.primitive.boundingBoxMax - result.primitive.boundingBoxMin;
    float3 objPos0 = DecodePosition(vertexPosition0, result.primitive.boundingBoxMin, extent);
    float3 objPos1 = DecodePosition(vertexPosition1, result.primitive.boundingBoxMin, extent);
    float3 objPos2 = DecodePosition(vertexPosition2, result.primitive.boundingBoxMin, extent);

    float4x4 mvp = mul(sceneData->viewProj, result.model.modelMatrix);
    float4 clip0 = mul(mvp, float4(objPos0, 1.0));
    float4 clip1 = mul(mvp, float4(objPos1, 1.0));
    float4 clip2 = mul(mvp, float4(objPos2, 1.0));
    float2 pixelNdc = (float2(pixel) + 0.5) / float2(extents) * 2.0 - 1.0;
    BarycentricDeriv baryDeriv = CalcFullBary(clip0, clip1, clip2, pixelNdc, float2(extents));
    float3 bary = baryDeriv.m_lambda;

    float2 uv0 = DecodeUV(vertexAttribute0);
    float2 uv1 = DecodeUV(vertexAttribute1);
    float2 uv2 = DecodeUV(vertexAttribute2);

    result.shadingInputs.mv = mul(sceneData->view, result.model.modelMatrix);
    result.shadingInputs.objPos = bary.x * objPos0 + bary.y * objPos1 + bary.z * objPos2;
    result.shadingInputs.posVS = mul(result.shadingInputs.mv, float4(result.shadingInputs.objPos, 1.0)).xyz;
    result.shadingInputs.interp = InterpolateVertex(bary, vertexAttribute0, vertexAttribute1, vertexAttribute2, result.shadingInputs.mv);
    result.shadingInputs.uvDdx = baryDeriv.m_ddx.x * uv0 + baryDeriv.m_ddx.y * uv1 + baryDeriv.m_ddx.z * uv2;
    result.shadingInputs.uvDdy = baryDeriv.m_ddy.x * uv0 + baryDeriv.m_ddy.y * uv1 + baryDeriv.m_ddy.z * uv2;

    float4 currClip = mul(sceneData->viewProj, mul(result.model.modelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float4 prevClip = mul(sceneData->prevViewProj, mul(result.model.prevModelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float2 currNDC = currClip.xy / currClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    result.shadingInputs.velocity = (prevNDC - currNDC) - sceneData->jitter + sceneData->prevJitter;

    // clip.w = +viewDepth with this projection, so this is the view-depth delta (viewZprev - viewZ) NRD expects as mv.z
    result.shadingInputs.viewZDelta = prevClip.w - currClip.w;

    result.valid = true;
    return result;
}
