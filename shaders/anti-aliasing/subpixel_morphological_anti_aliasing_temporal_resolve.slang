import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

// https://github.com/iryoku/smaa/blob/master/SMAA.hlsl
// SMAA T2X temporal resolve. Blends current and previous frames.
//
// Simple resolve: 50/50 blend, no reprojection. Use when no velocity buffer is available.
//
// Reprojection resolve: uses a velocity buffer to reproject the previous frame.
// Requires pass 3 to have packed velocity magnitude into the alpha channel:
//   color.a = sqrt(5.0 * length(velocity))
// Velocity buffer must store motion vectors as RG in [-1, 1] (snorm or encoded as unorm * 2 - 1).
// SMAA_REPROJECTION_WEIGHT_SCALE controls how aggressively velocity differences attenuate history.

static const float SMAA_REPROJECTION_WEIGHT_SCALE = 30.0;

// ============================================================
// Pass 4: Temporal Resolve (with reprojection via velocity buffer)
//
// Velocity buffer is sampled from rdgTexturesFloat2[pc.velocityIndex].
// Expects velocity in UV space (i.e. already divided by resolution).
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void TemporalResolveMain(uniform SmaaTemporalResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2   coord = int2(threadID.xy);
    float2 uv    = (float2(coord) + 0.5) * pc.sceneData.texelSize;

    // Velocity is negated for reprojection (motion blur vectors point forward, we need backward).
    float2 velocity = -rdgTexturesFloat2[pc.velocityIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv, 0);

    float4 current  = rdgTexturesFloat4[pc.currentColorIndex].Load(int3(coord, 0));
    float4 previous = rdgTexturesFloat4[pc.previousColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + velocity, 0);

    // Alpha encodes velocity magnitude packed by pass 3: sqrt(5 * |v|).
    // Attenuate history when current and previous velocities diverge significantly.
    float delta  = abs(current.a * current.a - previous.a * previous.a) / 5.0;
    float weight = 0.5 * saturate(1.0 - sqrt(delta) * SMAA_REPROJECTION_WEIGHT_SCALE);

    rdgStorageFloat4[pc.outputIndex][coord] = lerp(current, previous, weight);
}
