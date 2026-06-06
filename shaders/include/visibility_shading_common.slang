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
    public float worldZDelta;
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

public ReconstructedShadingData ReconstructShadingInputs(
    uint2 pixel,
    uint32_t visibilityBufferIndex,
    uint32_t barycentricBufferIndex,
    uint32_t derivativeBufferIndex,
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

    float2 bary2 = rdgStorageFloat2[barycentricBufferIndex].Load(pixel);
    float3 bary = float3(bary2.x, bary2.y, 1.0 - bary2.x - bary2.y);
    float4 derivs = rdgStorageFloat4[derivativeBufferIndex].Load(pixel);

    result.shadingInputs.mv = mul(sceneData->view, result.model.modelMatrix);
    float3 extent = result.primitive.boundingBoxMax - result.primitive.boundingBoxMin;
    result.shadingInputs.objPos = bary.x * DecodePosition(vertexPosition0, result.primitive.boundingBoxMin, extent)
                                + bary.y * DecodePosition(vertexPosition1, result.primitive.boundingBoxMin, extent)
                                + bary.z * DecodePosition(vertexPosition2, result.primitive.boundingBoxMin, extent);
    result.shadingInputs.posVS = mul(result.shadingInputs.mv, float4(result.shadingInputs.objPos, 1.0)).xyz;
    result.shadingInputs.interp = InterpolateVertex(bary, vertexAttribute0, vertexAttribute1, vertexAttribute2, result.shadingInputs.mv);
    result.shadingInputs.uvDdx = derivs.xy;
    result.shadingInputs.uvDdy = derivs.zw;

    float4 currClip = mul(sceneData->viewProj, mul(result.model.modelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float4 prevClip = mul(sceneData->prevViewProj, mul(result.model.prevModelMatrix, float4(result.shadingInputs.objPos, 1.0)));
    float2 currNDC = currClip.xy / currClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    result.shadingInputs.velocity = (prevNDC - currNDC) - sceneData->jitter + sceneData->prevJitter;

    float3 currWorldPos = mul(result.model.modelMatrix, float4(result.shadingInputs.objPos, 1.0)).xyz;
    float3 prevWorldPos = mul(result.model.prevModelMatrix, float4(result.shadingInputs.objPos, 1.0)).xyz;
    result.shadingInputs.worldZDelta = prevWorldPos.z - currWorldPos.z;

    result.valid = true;
    return result;
}
