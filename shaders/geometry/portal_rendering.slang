import common_interop;
import constants_interop;
import push_constant_interop;
import model_interop;
import instancing_interop;

import shader_constants;
import shader_functions;

import limits_declarations;
import bindless_declarations;

// ==================== Fragment Shader (Composite) ====================
struct CompositeOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float depth : SV_Depth;
};

[shader("fragment")]
CompositeOutput FragmentPortalComposite(uniform PortalCompositePushConstant pc, FullscreenPassVertexOutput input) {
    CompositeOutput output;

    output.color = rdgTexturesFloat4[pc.portalColorIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.velocity = rdgTexturesFloat4[pc.portalVelocityIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv);
    output.depth = rdgTexturesFloat[pc.portalDepthIndex].Sample(rdgSamplers[RDG_POINT_SAMPLER_INDEX], input.uv).r;

    return output;
}