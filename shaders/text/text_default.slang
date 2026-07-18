import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import text_interop;
import bindless_declarations;
import shader_functions;

// ==================== Mesh Shader ====================

static const uint QUADS_PER_GROUP = 16;
static const uint VERTS_PER_GROUP = QUADS_PER_GROUP * 4; // 64, follows the same limits we have for geometry
static const uint TRIS_PER_GROUP  = QUADS_PER_GROUP * 2; // 32, since text rendering is entirely unique quads

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation uint drawCallIndex : COLOR1;
    [[vk::location(3)]] nointerpolation float2 uvOrigMin : TEXCOORD2;
    [[vk::location(4)]] nointerpolation float2 uvOrigMax : TEXCOORD3;
    [[vk::location(5)]] nointerpolation uint2 stableId : COLOR2;
};

groupshared uint gs_visibleQuadIndices[QUADS_PER_GROUP];

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MeshTextDefault(
    OutputVertices<VertexOutput, VERTS_PER_GROUP> vertices,
    OutputIndices<uint3, TRIS_PER_GROUP> triangles,
    uint3 gid  : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    uniform TextRenderPushConstant pc)
{
    uint globalQuadBase = pc.quadOffset + gid.x * QUADS_PER_GROUP;
    uint batchEnd = pc.quadOffset + pc.quadCount;
    uint localQuadCount = (batchEnd > globalQuadBase) ? min(QUADS_PER_GROUP, batchEnd - globalQuadBase) : 0u;

    if (localQuadCount == 0) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint tid = gtid.x;

    // Frustum cull; z=0 in local space so only need columns 0, 1, 3 of the model matrix
    bool visible = false;
    if (tid < localQuadCount) {
        WorldGlyphQuad q = pc.worldGlyphQuads[globalQuadBase + tid];
        TextInstanceData inst = pc.textInstanceData[q.drawCallIndex];
        float4x4 M = pc.modelBuffer[inst.modelIndex].modelMatrix;

        float3 col0 = float3(M[0][0], M[1][0], M[2][0]);
        float3 col1 = float3(M[0][1], M[1][1], M[2][1]);
        float3 col3 = float3(M[0][3], M[1][3], M[2][3]);

        float3 c00 = col0 * q.posMin.x + col1 * q.posMin.y + col3;
        float3 c11 = col0 * q.posMax.x + col1 * q.posMax.y + col3;

        visible = IsAABBInFrustum(scene.frustum, min(c00, c11), max(c00, c11));

        if (visible) {
            gs_visibleQuadIndices[WavePrefixCountBits(visible)] = tid;
        }
    }

    // Wave intrinsics because xDispatch is 32 (NVIDIA wave size 32, AMD wave size 64 so we're safe)
    uint visibleCount = WaveActiveCountBits(visible);

    GroupMemoryBarrierWithGroupSync();

    SetMeshOutputCounts(visibleCount * 4, visibleCount * 2);

    if (visibleCount == 0) { return; }

    for (uint vi = tid; vi < visibleCount * 4; vi += 32) {
        uint quadIdx = gs_visibleQuadIndices[vi / 4];
        uint cornerIdx = vi % 4;

        WorldGlyphQuad q = pc.worldGlyphQuads[globalQuadBase + quadIdx];
        TextInstanceData inst = pc.textInstanceData[q.drawCallIndex];
        float4x4 M = pc.modelBuffer[inst.modelIndex].modelMatrix;

        float3 col0 = float3(M[0][0], M[1][0], M[2][0]);
        float3 col1 = float3(M[0][1], M[1][1], M[2][1]);
        float3 col3 = float3(M[0][3], M[1][3], M[2][3]);

        float px = (cornerIdx & 1u) != 0u ? q.posMax.x : q.posMin.x;
        float py = (cornerIdx & 2u) != 0u ? q.posMax.y : q.posMin.y;
        float3 worldXY = col0 * px + col1 * py + col3;

        float ux = (cornerIdx & 1u) != 0u ? q.uvMax.x : q.uvMin.x;
        // world top (posMax.y) maps to atlas low-V (uvMin.y); world bottom to high-V (uvMax.y)
        float uy = (cornerIdx & 2u) != 0u ? q.uvMin.y : q.uvMax.y;

        float4 clipPos = mul(scene.viewProj, float4(worldXY, 1.0));

        VertexOutput v;

        v.position = clipPos;
        v.uv = float2(ux, uy);
        v.color = q.color;
        v.drawCallIndex = q.drawCallIndex;
        v.uvOrigMin = q.uvOrigMin;
        v.uvOrigMax = q.uvOrigMax;
        v.stableId = uint2(inst.stableIdLo, inst.stableIdHi);
        vertices[vi] = v;
    }

    if (tid < visibleCount) {
        uint base = tid * 4;
        // CCW winding: corners 0=BL, 1=BR, 2=TL, 3=TR
        triangles[tid * 2 + 0] = uint3(base + 0, base + 1, base + 2);
        triangles[tid * 2 + 1] = uint3(base + 1, base + 3, base + 2);
    }
}

// ==================== Fragment Shader ====================
float MSDFMedian(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

struct FragmentOutput
{
    float4 color   : SV_Target0;
    uint2 stableId : SV_Target1;
};

[shader("fragment")]
FragmentOutput FragmentTextDefault(VertexOutput input, uniform TextRenderPushConstant pc)
{
    TextInstanceData inst = pc.textInstanceData[input.drawCallIndex];
    TextRenderMaterial mat = pc.textMaterialBuffer[pc.textMaterialIndex];

    Texture2D atlasTex = assetTextures[pc.atlasBindlessIndex];
    SamplerState smp = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
    float screenPxRange = MSDFScreenPxRange(atlasTex, input.uv, inst.pxRange);

    float4 msd = atlasTex.Sample(smp, clamp(input.uv, input.uvOrigMin, input.uvOrigMax));
    float sd = msd.a;
    float screenPxDist = screenPxRange * (sd - 0.5);
    float fillOpacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

    float4 baseColor = input.color * mat.colorTint;

    float4 result;
    if (mat.shadowColor.a > 0.0 && (mat.shadowOffset.x != 0.0 || mat.shadowOffset.y != 0.0)) {
        float2 shadowUv = input.uv - mat.shadowOffset;
        float4 shadowSample = atlasTex.Sample(smp, clamp(shadowUv, input.uvOrigMin, input.uvOrigMax));
        float shadowPxDist = screenPxRange * (shadowSample.a - 0.5);
        float shadowEdge = max(screenPxRange * mat.shadowSoftness, 0.001);
        float shadowOpacity = clamp(shadowPxDist / shadowEdge + 0.5, 0.0, 1.0);
        result = float4(mat.shadowColor.rgb, mat.shadowColor.a * shadowOpacity);
    } else {
        result = float4(0.0, 0.0, 0.0, 0.0);
    }

    float4 glyphColor;
    if (mat.outlineColor.a > 0.0 && mat.outlineWidth > 0.0) {
        float outlineOpacity = clamp(screenPxDist + 0.5 + screenPxRange * min(mat.outlineWidth, 0.475), 0.0, 1.0);
        glyphColor = lerp(float4(mat.outlineColor.rgb, mat.outlineColor.a * outlineOpacity), float4(baseColor.rgb, baseColor.a * fillOpacity), fillOpacity);
    } else {
        glyphColor = float4(baseColor.rgb, baseColor.a * fillOpacity);
    }

    result = lerp(result, glyphColor, glyphColor.a);

    if (result.a < 0.001) { discard; }

    FragmentOutput output;
    output.color = result;
    output.stableId = input.stableId;
    return output;
}
