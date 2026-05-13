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
    float4 position      : SV_Position;
    float2 uv            : TEXCOORD0;
    float4 color         : COLOR0;
    nointerpolation uint drawCallIndex : COLOR1;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MeshMain(
    OutputVertices<VertexOutput, VERTS_PER_GROUP> vertices,
    OutputIndices<uint3, TRIS_PER_GROUP> triangles,
    uint3 gid  : SV_GroupID,
    uint3 gtid : SV_GroupThreadID,
    uniform TextRenderPushConstant pc)
{
    uint globalQuadBase = pc.quadOffset + gid.x * QUADS_PER_GROUP;
    uint batchEnd = pc.quadOffset + pc.quadCount;

    uint localQuadCount = (batchEnd > globalQuadBase) ? min(QUADS_PER_GROUP, batchEnd - globalQuadBase) : 0u;

    SetMeshOutputCounts(localQuadCount * 4, localQuadCount * 2);

    if (localQuadCount == 0) { return; }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint tid = gtid.x;

    for (uint vi = tid; vi < localQuadCount * 4; vi += 32) {
        uint quadIdx = vi / 4;
        uint cornerIdx = vi % 4;

        GlyphQuad q = pc.glyphQuads[globalQuadBase + quadIdx];
        TextInstanceData inst = pc.textInstanceData[q.drawCallIndex];
        Model m = pc.modelBuffer[inst.modelIndex];

        // bit 0 = right (x), bit 1 = top (y)
        float px = (cornerIdx & 1u) != 0u ? q.posMax.x : q.posMin.x;
        float py = (cornerIdx & 2u) != 0u ? q.posMax.y : q.posMin.y;
        float ux = (cornerIdx & 1u) != 0u ? q.uvMax.x  : q.uvMin.x;
        // world top (posMax.y) maps to atlas low-V (uvMin.y); world bottom to high-V (uvMax.y)
        float uy = (cornerIdx & 2u) != 0u ? q.uvMin.y  : q.uvMax.y;

        float4 worldPos = mul(m.modelMatrix, float4(px, py, 0.0, 1.0));
        float4 clipPos  = mul(scene.viewProj, worldPos);

        VertexOutput v;
        v.position = clipPos;
        v.uv = float2(ux, uy);
        v.color = q.color;
        v.drawCallIndex = q.drawCallIndex;
        vertices[vi] = v;
    }

    if (tid < localQuadCount) {
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

[shader("fragment")]
float4 FragmentMain(VertexOutput input, uniform TextRenderPushConstant pc) : SV_Target0
{
    TextInstanceData inst = pc.textInstanceData[input.drawCallIndex];

    Texture2D atlasTex = assetTextures[inst.atlasBindlessIndex];
    float4 msd = atlasTex.Sample(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], input.uv);
    float screenPxRange = MSDFScreenPxRange(atlasTex, input.uv, inst.pxRange);

    float sd = msd.a;
    float screenPxDist = screenPxRange * (sd - 0.5);
    float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

    if (opacity < 0.001) { discard; }

    return float4(input.color.rgb, input.color.a * opacity);
    // return float4(msd.rgb, 1.0f);
}
