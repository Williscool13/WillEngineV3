import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

static const float SMAA_REPROJECTION_WEIGHT_SCALE = 30.0;

[shader("compute")]
[numthreads(16, 16, 1)]
void TemporalResolveMain(uniform SmaaTemporalResolvePushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2   coord = int2(threadID.xy);
    float2 uv    = (float2(coord) + 0.5) * pc.sceneData.texelSize;

    uint4  g1          = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(coord, 0));
    float2 velocityNDC = UnpackMotionVectors(g1.g);
    float2 velocity    = velocityNDC * 0.5;

    float4 current  = rdgTexturesFloat4[pc.currentColorIndex].Load(int3(coord, 0));
    float4 previous = rdgTexturesFloat4[pc.previousColorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], uv + velocity, 0);

    float delta  = length(velocity);
    float weight = 0.5 * saturate(1.0 - delta * SMAA_REPROJECTION_WEIGHT_SCALE);

    rdgStorageFloat4[pc.outputIndex][coord] = lerp(current, previous, weight);
}
