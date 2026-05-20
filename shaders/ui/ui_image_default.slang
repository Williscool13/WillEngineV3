import push_constant_interop;
import bindless_declarations;
import constants_interop;
import ui_functions;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float2 clayPx; // Clay pixel coords (y=0 top), interpolated across quad
};

[shader("vertex")]
VertexOutput VertexMain(uint vid : SV_VertexID, uniform UIImagePushConstant pc)
{
    float px = (vid & 1u) != 0u ? pc.ndcMax.x : pc.ndcMin.x;
    float py = (vid & 2u) != 0u ? pc.ndcMax.y : pc.ndcMin.y;
    float ux = (vid & 1u) != 0u ? pc.uvMax.x : pc.uvMin.x;
    float uy = (vid & 2u) != 0u ? pc.uvMax.y : pc.uvMin.y;
    float cpx = (vid & 1u) != 0u ? pc.pxMax.x : pc.pxMin.x;
    float cpy = (vid & 2u) != 0u ? pc.pxMax.y : pc.pxMin.y;
    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.uv = float2(ux, uy);
    o.clayPx = float2(cpx, cpy);
    return o;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input, uniform UIImagePushConstant pc) : SV_Target0
{
    Texture2D img = assetTextures[pc.imageBindlessIndex];
    SamplerState smp = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
    float4 sampled = img.Sample(smp, input.uv) * pc.tintColor;

    float2 center = (pc.pxMin + pc.pxMax) * 0.5;
    float2 halfSize = (pc.pxMax - pc.pxMin) * 0.5;
    float d = sdRoundedBoxPerCorner(input.clayPx - center, halfSize, pc.cornerRadius);
    float alpha = sampled.a * smoothstep(1.0, -1.0, d);
    if (alpha < 0.001) { discard; }
    return float4(sampled.rgb, alpha);
}
