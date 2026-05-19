import common_interop;
import push_constant_interop;
import text_interop;
import constants_interop;

import bindless_declarations;
import shader_functions;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] nointerpolation float2 uvOrigMin;
    [[vk::location(3)]] nointerpolation float2 uvOrigMax;
};

[shader("vertex")]
VertexOutput VertexMain(uint vid : SV_VertexID, uint iid : SV_InstanceID, uniform UITextRenderPushConstant pc)
{
    uint glyphIdx = pc.quadOffset + iid;
    uint corner = vid;
    UIGlyphQuad q = pc.uiGlyphQuads[glyphIdx];

    float px = (corner & 1u) ? q.posMax.x : q.posMin.x;
    float py = (corner & 2u) ? q.posMax.y : q.posMin.y;
    float ux = (corner & 1u) ? q.uvMax.x : q.uvMin.x;
    // Screen space: posMin.y is glyph top (smaller NDC Y); atlas V=0 is top, so top maps to uvMin.y
    float uy = (corner & 2u) ? q.uvMax.y : q.uvMin.y;

    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.uv = float2(ux, uy);
    o.color = q.color;
    o.uvOrigMin = q.uvOrigMin;
    o.uvOrigMax = q.uvOrigMax;
    return o;
}

float MSDFMedian(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input, uniform UITextRenderPushConstant pc) : SV_Target0
{
    Texture2D atlas = assetTextures[pc.atlasBindlessIndex];
    SamplerState smp = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];

    float screenPxRange = MSDFScreenPxRange(atlas, input.uv, pc.pxRange);
    float4 msd = atlas.Sample(smp, clamp(input.uv, input.uvOrigMin, input.uvOrigMax));
    float sd = msd.a;
    float screenPxDist = screenPxRange * (sd - 0.5);
    float fillOpacity = clamp(screenPxDist + 0.5, 0.0, 1.0);

    float4 result = float4(input.color.rgb, input.color.a * fillOpacity);
    if (result.a < 0.001) { discard; }
    return result;
}
