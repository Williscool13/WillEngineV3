import push_constant_interop;
import ui_functions;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
    [[vk::location(1)]] float2 clayPx; // Clay pixel coords (y=0 top), interpolated across quad
};

[shader("vertex")]
VertexOutput VertexUIRectDefault(uint vid : SV_VertexID, uniform UIRectRenderPushConstant pc)
{
    float px = (vid & 1u) != 0u ? pc.ndcMax.x : pc.ndcMin.x;
    float py = (vid & 2u) != 0u ? pc.ndcMax.y : pc.ndcMin.y;
    float cpx = (vid & 1u) != 0u ? pc.pxMax.x : pc.pxMin.x;
    float cpy = (vid & 2u) != 0u ? pc.pxMax.y : pc.pxMin.y;
    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.color = pc.color;
    o.clayPx = float2(cpx, cpy);
    return o;
}

[shader("fragment")]
float4 FragmentUIRectDefault(VertexOutput input, uniform UIRectRenderPushConstant pc) : SV_Target0
{
    float2 center = (pc.pxMin + pc.pxMax) * 0.5;
    float2 halfSize = (pc.pxMax - pc.pxMin) * 0.5;
    float d = sdRoundedBoxPerCorner(input.clayPx - center, halfSize, pc.cornerRadius);
    float alpha = input.color.a * smoothstep(1.0, -1.0, d);
    if (alpha < 0.001) { discard; }
    return float4(input.color.rgb, alpha);
}
