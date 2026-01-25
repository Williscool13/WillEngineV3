import common_interop;
import constants_interop;
import push_constant_interop;

import shader_constants;

import bindless_declarations;


struct FragmentOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float depth : SV_Depth;
};

[shader("fragment")]
FragmentOutput FragmentMain(uniform PortalCompositePushConstant pc, FullscreenPassVertexOutput input) {
    FragmentOutput output;

    output.color = rdgTextures[pc.portalColorIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.velocity = rdgTextures[pc.portalVelocityIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.depth = rdgTextures[pc.portalDepthIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv).r;

    return output;
}