import common_interop;
import push_constant_interop;
import text_interop;
import constants_interop;

import bindless_declarations;
import slug_functions;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 em;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] nointerpolation uint glyphTexelOffset;
};

[shader("vertex")]
VertexOutput VertexUITextDefault(uint vid : SV_VertexID, uint iid : SV_InstanceID, uniform UITextRenderPushConstant pc)
{
    uint glyphIdx = pc.quadOffset + iid;
    uint corner = vid;
    UIGlyphQuad q = pc.uiGlyphQuads[glyphIdx];

    float px = (corner & 1u) != 0u ? q.posMax.x : q.posMin.x;
    float py = (corner & 2u) != 0u ? q.posMax.y : q.posMin.y;
    // posMin.y is glyph top on screen; emMin.y carries planeTop so em interpolates correctly under y-down NDC
    float ex = (corner & 1u) != 0u ? q.emMax.x : q.emMin.x;
    float ey = (corner & 2u) != 0u ? q.emMax.y : q.emMin.y;

    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.em = float2(ex, ey);
    o.color = q.color;
    o.glyphTexelOffset = q.glyphTexelOffset;
    return o;
}

[shader("fragment")]
float4 FragmentUITextDefault(VertexOutput input, uniform UITextRenderPushConstant pc) : SV_Target0
{
    float coverage = SlugDraw(pc.fontCurveTexels, input.em, input.glyphTexelOffset);

    float4 result = float4(input.color.rgb, input.color.a * coverage) * pc.colorTint;
    if (result.a < 0.001) { discard; }
    return result;
}
