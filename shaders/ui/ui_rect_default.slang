import push_constant_interop;
import ui_interop;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
};

[shader("vertex")]
VertexOutput VertexMain(uint vid : SV_VertexID, uint iid : SV_InstanceID, uniform UIRectRenderPushConstant pc)
{
    UIRectData rect = pc.rects[pc.rectOffset + iid];
    float px = (vid & 1u) ? rect.posMax.x : rect.posMin.x;
    float py = (vid & 2u) ? rect.posMax.y : rect.posMin.y;
    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.color = rect.color;
    return o;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target0
{
    if (input.color.a < 0.001) { discard; }
    return input.color;
}
