import push_constant_interop;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
};

[shader("vertex")]
VertexOutput VertexMain(uint vid : SV_VertexID, uniform UIRectRenderPushConstant pc)
{
    float px = (vid & 1u) != 0u ? pc.posMax.x : pc.posMin.x;
    float py = (vid & 2u) != 0u ? pc.posMax.y : pc.posMin.y;
    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.color = pc.color;
    return o;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target0
{
    if (input.color.a < 0.001) { discard; }
    return input.color;
}
