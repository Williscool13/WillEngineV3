import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

[shader("compute")]
[numthreads(POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X, POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y, 1)]
void ComputeVignetteAberration(uniform VignetteChromaticAberrationPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }

    uint2 pixelCoord = dtid.xy;
    float2 uv = (dtid.xy + 0.5) / pc.outputExtent;
    float2 centerOffset = uv - 0.5;
    float distFromCenter = length(centerOffset);

    // Chromatic aberration
    float2 direction = distFromCenter > 0.0001 ? normalize(centerOffset) : float2(0, 0);
    float aberrationOffset = distFromCenter * pc.chromaticAberrationStrength / pc.outputExtent.x;
    float2 uvR = uv + direction * aberrationOffset;
    float2 uvG = uv;
    float2 uvB = uv - direction * aberrationOffset;
    float r = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvR, 0).r;
    float g = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvG, 0).g;
    float b = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uvB, 0).b;
    float3 color = float3(r, g, b);

    // Vignette
    float vignette = smoothstep(
        pc.vignetteRadius,
        pc.vignetteRadius - pc.vignetteSmoothness,
        distFromCenter
    );
    vignette = lerp(1.0, vignette, pc.vignetteStrength);
    color *= vignette;
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
