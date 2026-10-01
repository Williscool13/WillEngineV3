module visibility_shading_common;

import common_interop;
import constants_interop;
import model_interop;
import instancing_interop;
import push_constant_interop;
import shader_functions;
import bindless_declarations;
import material_uv_common;

public struct ShadingInputs
{
    public float3 objPos;
    public float3 posVS;
    public InterpolatedVertex interp;
    public float2 uvDdx;
    public float2 uvDdy;
    public float3 normalOS;
    // Screen derivatives of objPos, scaled like uvDdx/uvDdy
    public float3 objPosDdx;
    public float3 objPosDdy;
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
    public float3 shadowOriginOffsetWS;
}

/** Texture coordinate sets for one pixel: the mesh UV, or up to 3 weighted triplanar axes. Frames are view space. */
public struct SurfaceUVLayers
{
    public uint count;
    public float weight[3];
    public float2 uv[3];
    public float2 ddx[3];
    public float2 ddy[3];
    public float3 tangentVS[3];
    public float3 bitangentVS[3];
}

/**
 * @param N view-space vertex normal (after any two-sided flip).
 * @param T, B view-space tangent frame of the mesh UV.
 */
public SurfaceUVLayers BuildSurfaceUVLayers(MaterialProperties material, ShadingInputs si, float4x4 modelMatrix, float3 N, float3 T, float3 B)
{
    SurfaceUVLayers layers;
    if (material.uvMode == MATERIAL_UV_MESH) {
        layers.count = 1u;
        layers.weight[0] = 1.0;
        layers.uv[0] = si.interp.uv;
        layers.ddx[0] = si.uvDdx;
        layers.ddy[0] = si.uvDdy;
        layers.tangentVS[0] = T;
        layers.bitangentVS[0] = B;
        return layers;
    }

    bool bWorld = material.uvMode == MATERIAL_UV_TRIPLANAR_WORLD;
    float3 scale = ModelScale(modelMatrix);
    float3x3 m3 = (float3x3)modelMatrix;
    float3 p = bWorld ? mul(modelMatrix, float4(si.objPos, 1.0)).xyz : si.objPos * scale;
    float3 dpdx = bWorld ? mul(m3, si.objPosDdx) : si.objPosDdx * scale;
    float3 dpdy = bWorld ? mul(m3, si.objPosDdy) : si.objPosDdy * scale;
    float3 n = bWorld ? normalize(WorldNormalDirection(modelMatrix, si.normalOS)) : si.normalOS;
    float3 w = TriplanarWeights(n);
    float3x3 mv3 = (float3x3)si.mv;
    layers.count = 0u;
    for (uint axis = 0u; axis < 3u; axis++) {
        if (w[axis] <= 0.0) {
            continue;
        }
        uint i = layers.count++;
        layers.weight[i] = w[axis];
        layers.uv[i] = TriplanarAxis(p, n, axis);
        layers.ddx[i] = TriplanarAxis(dpdx, n, axis);
        layers.ddy[i] = TriplanarAxis(dpdy, n, axis);
        float3 tangentOS;
        float3 bitangentOS;
        TriplanarAxisFrame(n, axis, tangentOS, bitangentOS);
        if (bWorld) {
            // World directions back through the inverse model matrix, so mv3 below lands them in view space.
            float3 invScaleSq = 1.0 / max(scale * scale, 1e-12);
            tangentOS = mul(tangentOS, m3) * invScaleSq;
            bitangentOS = mul(bitangentOS, m3) * invScaleSq;
        }
        float3 t = mul(mv3, tangentOS);
        float3 b = mul(mv3, bitangentOS);
        layers.tangentVS[i] = normalize(t - N * dot(N, t));
        layers.bitangentVS[i] = normalize(b - N * dot(N, b));
    }
    return layers;
}

public float4 SampleSurfaceLayers(SurfaceUVLayers layers, int imageIndex, int samplerIndex, float4 uvTransform)
{
    float4 result = float4(0.0);
    for (uint i = 0u; i < layers.count; i++) {
        result += layers.weight[i] * assetTextures[NonUniformResourceIndex(imageIndex)].SampleGrad(assetSamplers[NonUniformResourceIndex(samplerIndex)], layers.uv[i] * uvTransform.xy + uvTransform.zw, layers.ddx[i] * uvTransform.xy, layers.ddy[i] * uvTransform.xy);
    }
    return result;
}

/** Tangent-space normal map per layer, blended in view space. */
public float3 SampleSurfaceLayersNormal(SurfaceUVLayers layers, int imageIndex, int samplerIndex, float4 uvTransform, float normalScale, float3 N)
{
    float3 blended = float3(0.0);
    for (uint i = 0u; i < layers.count; i++) {
        float2 normalRG = assetTextures[NonUniformResourceIndex(imageIndex)].SampleGrad(assetSamplers[NonUniformResourceIndex(samplerIndex)], layers.uv[i] * uvTransform.xy + uvTransform.zw, layers.ddx[i] * uvTransform.xy, layers.ddy[i] * uvTransform.xy).xy;
        float3 tsNormal;
        tsNormal.xy = normalRG * 2.0 - 1.0;
        tsNormal.z = sqrt(saturate(1.0 - dot(tsNormal.xy, tsNormal.xy)));
        tsNormal.xy *= normalScale;
        tsNormal = normalize(tsNormal);
        blended += layers.weight[i] * normalize(tsNormal.x * layers.tangentVS[i] + tsNormal.y * layers.bitangentVS[i] + tsNormal.z * N);
    }
    return normalize(blended);
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
    bool bTwoSided,
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

    float normalSign = 1.0;
    if (bTwoSided) {
        float3x3 mv3 = (float3x3)result.shadingInputs.mv;
        float3 faceN = cross(mul(mv3, objPos1 - objPos0), mul(mv3, objPos2 - objPos0));
        if (dot(faceN, result.shadingInputs.interp.normalVS) * dot(faceN, result.shadingInputs.posVS) > 0.0) {
            normalSign = -1.0;
            result.shadingInputs.interp.normalVS = -result.shadingInputs.interp.normalVS;
            result.shadingInputs.interp.tangentVS = -result.shadingInputs.interp.tangentVS;
            result.shadingInputs.interp.tangentSign = -result.shadingInputs.interp.tangentSign;
        }
    }
    result.shadingInputs.uvDdx = (baryDeriv.m_ddx.x * uv0 + baryDeriv.m_ddx.y * uv1 + baryDeriv.m_ddx.z * uv2) * sceneData->uvDerivativeScale;
    result.shadingInputs.uvDdy = (baryDeriv.m_ddy.x * uv0 + baryDeriv.m_ddy.y * uv1 + baryDeriv.m_ddy.z * uv2) * sceneData->uvDerivativeScale;
    result.shadingInputs.objPosDdx = (baryDeriv.m_ddx.x * objPos0 + baryDeriv.m_ddx.y * objPos1 + baryDeriv.m_ddx.z * objPos2) * sceneData->uvDerivativeScale;
    result.shadingInputs.objPosDdy = (baryDeriv.m_ddy.x * objPos0 + baryDeriv.m_ddy.y * objPos1 + baryDeriv.m_ddy.z * objPos2) * sceneData->uvDerivativeScale;
    result.shadingInputs.normalOS = normalize(bary.x * DecodeNormal(vertexAttribute0) + bary.y * DecodeNormal(vertexAttribute1) + bary.z * DecodeNormal(vertexAttribute2));

    float4 currClip = mul(sceneData->viewProj, mul(result.model.modelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float4 prevClip = mul(sceneData->prevViewProj, mul(result.model.prevModelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float2 currNDC = currClip.xy / currClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    result.shadingInputs.velocity = (prevNDC - currNDC) - sceneData->jitter + sceneData->prevJitter;

    // clip.w = +viewDepth with this projection, so this is the view-depth delta (viewZprev - viewZ) NRD expects as mv.z
    result.shadingInputs.viewZDelta = prevClip.w - currClip.w;

    // Hanika shadow terminator offset.
    float3x3 model3 = (float3x3)result.model.modelMatrix;
    float3x3 normalMatrix = Adjugate(model3);
    float3 posWS = mul(model3, result.shadingInputs.objPos);
    float3 n0 = normalize(mul(normalMatrix, DecodeNormal(vertexAttribute0))) * normalSign;
    float3 n1 = normalize(mul(normalMatrix, DecodeNormal(vertexAttribute1))) * normalSign;
    float3 n2 = normalize(mul(normalMatrix, DecodeNormal(vertexAttribute2))) * normalSign;
    float3 d0 = min(0.0, dot(posWS - mul(model3, objPos0), n0)) * n0;
    float3 d1 = min(0.0, dot(posWS - mul(model3, objPos1), n1)) * n1;
    float3 d2 = min(0.0, dot(posWS - mul(model3, objPos2), n2)) * n2;
    result.shadowOriginOffsetWS = -(bary.x * d0 + bary.y * d1 + bary.z * d2);

    result.valid = true;
    return result;
}
