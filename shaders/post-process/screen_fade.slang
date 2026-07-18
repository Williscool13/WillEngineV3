import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;
import random_common;

import bindless_declarations;

// Each mode maps progress onto an edge that starts fully outside the screen and ends fully past it, so
// progress 0 and 1 are exactly uncovered/covered regardless of softness.
float ScreenFadeCoverage(ScreenFadePushConstant pc, float2 uv, uint2 pixelCoord) {
    float soft = max(pc.softness, 1e-4);

    switch (pc.mode) {
        case SCREEN_FADE_MODE_IRIS: {
            float2 d = (uv - pc.center) * float2(pc.aspect, 1.0);
            float2 c0 = (float2(0.0, 0.0) - pc.center) * float2(pc.aspect, 1.0);
            float2 c1 = (float2(1.0, 1.0) - pc.center) * float2(pc.aspect, 1.0);
            float maxRadius = length(max(abs(c0), abs(c1)));
            float radius = lerp(maxRadius + soft, -soft, pc.progress);
            return smoothstep(radius - soft, radius + soft, length(d));
        }
        case SCREEN_FADE_MODE_WIPE: {
            float2 dir = pc.direction;
            float lenSq = dot(dir, dir);
            dir = lenSq > 1e-8 ? dir * rsqrt(lenSq) : float2(1.0, 0.0);
            float span = abs(dir.x) + abs(dir.y);
            float s = (dot(uv - 0.5, dir) / span) + 0.5;
            float edge = pc.progress * (1.0 + 2.0 * soft) - soft;
            return 1.0 - smoothstep(edge - soft, edge + soft, s);
        }
        case SCREEN_FADE_MODE_DISSOLVE: {
            float noise = SpatioTemporalBlueNoise2D(pixelCoord, 0u, 1u).x;
            float edge = pc.progress * (1.0 + 2.0 * soft) - soft;
            return smoothstep(noise - soft, noise + soft, edge);
        }
        case SCREEN_FADE_MODE_LETTERBOX: {
            float d = min(uv.y, 1.0 - uv.y);
            float edge = pc.progress * 0.5 * (1.0 + 2.0 * soft) - soft;
            return 1.0 - smoothstep(edge - soft, edge + soft, d);
        }
        default:
            return pc.progress;
    }
}

[shader("compute")]
[numthreads(SCREEN_FADE_DISPATCH_X, SCREEN_FADE_DISPATCH_Y, 1)]
void ComputeScreenFade(uniform ScreenFadePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }
    uint2 pixelCoord = dtid.xy;

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);
    float coverage = saturate(ScreenFadeCoverage(pc, uv, pixelCoord));

    float3 color = rdgTexturesFloat4[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;
    color = lerp(color, pc.color.rgb, coverage);

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
