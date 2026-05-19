import push_constant_interop;
import bindless_declarations;
import constants_interop;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

[shader("vertex")]
VertexOutput VertexMain(uint vid : SV_VertexID, uniform UIImagePushConstant pc)
{
    float px = (vid & 1u) ? pc.posMax.x : pc.posMin.x;
    float py = (vid & 2u) ? pc.posMax.y : pc.posMin.y;
    float ux = (vid & 1u) ? pc.uvMax.x : pc.uvMin.x;
    float uy = (vid & 2u) ? pc.uvMax.y : pc.uvMin.y;

    VertexOutput o;
    o.position = float4(px, py, 0.0, 1.0);
    o.uv = float2(ux, uy);
    return o;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input, uniform UIImagePushConstant pc) : SV_Target0
{
    Texture2D img = assetTextures[pc.imageBindlessIndex];
    SamplerState smp = assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX];
    float4 sampled = img.Sample(smp, input.uv);
    return sampled * pc.tintColor;
}
