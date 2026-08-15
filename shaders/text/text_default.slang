import common_interop;
import constants_interop;
import model_interop;
import push_constant_interop;
import text_interop;
import bindless_declarations;
import shader_functions;
import slug_functions;

// ==================== Mesh Shader ====================

static const uint QUADS_PER_GROUP = 16;
static const uint VERTS_PER_GROUP = QUADS_PER_GROUP * 4; // 64, follows the same limits we have for geometry
static const uint TRIS_PER_GROUP  = QUADS_PER_GROUP * 2; // 32, since text rendering is entirely unique quads

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 em : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] nointerpolation uint drawCallIndex : COLOR1;
    [[vk::location(3)]] nointerpolation uint glyphTexelOffset : TEXCOORD2;
    [[vk::location(4)]] nointerpolation uint2 stableId : COLOR2;
    [[vk::location(5)]] float2 sdfUv : TEXCOORD3;
    [[vk::location(6)]] nointerpolation float2 sdfUvScale : TEXCOORD4;
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
        float ex = (cornerIdx & 1u) != 0u ? q.emMax.x : q.emMin.x;
        float ey = (cornerIdx & 2u) != 0u ? q.emMax.y : q.emMin.y;

        float2 n = float2((cornerIdx & 1u) != 0u ? 0.70710678 : -0.70710678,
                          (cornerIdx & 2u) != 0u ? 0.70710678 : -0.70710678);

        float3 worldXY = col0 * px + col1 * py + col3;
        float3 worldN = col0 * n.x + col1 * n.y;

        float4 clipPos = mul(scene.viewProj, float4(worldXY, 1.0));
        float4 clipN = mul(scene.viewProj, float4(worldN, 0.0));

        // Slug dilation (hb_gpu_dilate
        float s = clipPos.w;
        float t = clipN.w;
        float u = (s * clipN.x - t * clipPos.x) * scene.mainRenderTargetSize.x;
        float v = (s * clipN.y - t * clipPos.y) * scene.mainRenderTargetSize.y;
        float s2 = s * s;
        float st = s * t;
        float uv2 = u * u + v * v;
        float denom = uv2 - st * st;
        float d = abs(denom) > (1.0 / 16777216.0) ? s2 * (st + sqrt(uv2)) / denom : 0.0;

        clipPos += d * clipN;
        float2 emPerLocal = (q.emMax - q.emMin) / (q.posMax - q.posMin);
        float2 emOut = float2(ex, ey) + d * n * emPerLocal;

        VertexOutput vtx;
        vtx.position = clipPos;
        vtx.em = emOut;
        vtx.color = q.color;
        vtx.drawCallIndex = q.drawCallIndex;
        vtx.glyphTexelOffset = q.glyphTexelOffset;
        vtx.stableId = uint2(inst.stableIdLo, inst.stableIdHi);
        vtx.sdfUv = emOut * q.sdfUvScale + q.sdfUvBias;
        vtx.sdfUvScale = q.sdfUvScale;
        vertices[vi] = vtx;
    }

    if (tid < visibleCount) {
        uint base = tid * 4;
        // CCW winding: corners 0=BL, 1=BR, 2=TL, 3=TR
        triangles[tid * 2 + 0] = uint3(base + 0, base + 1, base + 2);
        triangles[tid * 2 + 1] = uint3(base + 1, base + 3, base + 2);
    }
}

// ==================== Fragment Shader ====================

struct FragmentOutput
{
    float4 color   : SV_Target0;
    uint2 stableId : SV_Target1;
};

[shader("fragment")]
FragmentOutput FragmentTextDefault(VertexOutput input, uniform TextRenderPushConstant pc)
{
    TextRenderMaterial mat = pc.textMaterialBuffer[pc.textMaterialIndex];

    float2 pixelsPerEm = 1.0 / fwidth(input.em);
    SlugGlyphInfo gi = SlugDecodeGlyph(pc.fontCurveTexels, input.em, input.glyphTexelOffset);
    float ppem = SlugPpem(gi, pixelsPerEm);

    float fillOpacity = SlugCoverage(pc.fontCurveTexels, input.em, pixelsPerEm, input.glyphTexelOffset);

    bool bOutline = mat.outlineColor.a > 0.0 && mat.outlineWidth > 0.0;
    bool bShadow = mat.shadowColor.a > 0.0 && (mat.shadowOffset.x != 0.0 || mat.shadowOffset.y != 0.0);

    Texture2D sdfTex = assetTextures[pc.fontAtlasIndex];
    SamplerState sdfSampler = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];

    float4 baseColor = input.color * mat.colorTint;

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    if (bShadow) {
        float2 uvPerEm = input.sdfUvScale * gi.scale;
        float2 shadowUv = input.sdfUv - mat.shadowOffset * uvPerEm;
        float2 gridDims = float2(float(pc.sdfGridDims & 0xFFFFu), float(pc.sdfGridDims >> 16u));
        uint atlasW, atlasH;
        sdfTex.GetDimensions(atlasW, atlasH);
        float2 halfTexel = 0.5 / float2(float(atlasW), float(atlasH));
        float2 cellMin = floor(input.sdfUv * gridDims) / gridDims;
        shadowUv = clamp(shadowUv, cellMin + halfTexel, cellMin + 1.0 / gridDims - halfTexel);
        float sdEm = (sdfTex.Sample(sdfSampler, shadowUv).r - 0.5) * (2.0 * SDF_SPREAD_EM);
        float blurEm = clamp(mat.shadowSoftness, 1.0 / ppem, SDF_SPREAD_EM);
        float shadowOpacity = smoothstep(-blurEm, blurEm, sdEm);
        result = float4(mat.shadowColor.rgb, mat.shadowColor.a * shadowOpacity);
    }

    float4 glyphColor;
    if (bOutline) {
        float sdEm = (sdfTex.Sample(sdfSampler, input.sdfUv).r - 0.5) * (2.0 * SDF_SPREAD_EM);
        float outlineOpacity = clamp((sdEm + mat.outlineWidth) * ppem + 0.5, 0.0, 1.0);
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
