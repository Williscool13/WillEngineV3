import common_interop;
import constants_interop;
import push_constant_interop;
import bindless_declarations;

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput VertexMain(uint vertexID : SV_VertexID) {
    VertexOutput output;

    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float depth : SV_Depth;
};

[shader("fragment")]
FragmentOutput FragmentMain(uniform PortalCompositePushConstant pc, VertexOutput input) {
    FragmentOutput output;

    output.color = rdgTextures[pc.portalColorIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.velocity = rdgTextures[pc.portalVelocityIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.depth = rdgTextures[pc.portalDepthIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv).r;

    return output;
}