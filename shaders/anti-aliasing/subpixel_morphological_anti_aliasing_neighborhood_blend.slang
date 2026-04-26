import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

// https://github.com/iryoku/smaa/blob/master/SMAA.hlsl

// ============================================================
// Pass 3: Neighborhood Blending
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void NeighborhoodBlendMain(uniform SmaaNeighborhoodBlendPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2   coord  = int2(threadID.xy);
    float2 uv     = (float2(coord) + 0.5) * pc.sceneData.texelSize;
    float2 tsize  = pc.sceneData.texelSize;

    // Gather blend weights from the neighborhood.
    // Weight packing from pass 2: rg = horizontal edge weights, ba = vertical edge weights.
    // To blend rightward: read the right pixel's .a (its vertical-left weight pointing back at us).
    // To blend downward:  read the below pixel's .g (its horizontal-top weight pointing back at us).
    float4 a;
    a.x = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2(1, 0), 0)).a; // right pixel
    a.y = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord + int2(0, 1), 0)).g; // below pixel
    a.z = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,              0)).b; // current
    a.w = rdgTexturesFloat4[pc.blendWeightIndex].Load(int3(coord,              0)).r; // current

    float3 result = float3(0.0f);

    if (dot(a, float4(1.0)) < 1e-5) {
        result = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord, 0)).rgb;
    } else {
        // Horizontal dominates when right/left weights exceed top/bottom weights.
        bool h = max(a.x, a.z) > max(a.y, a.w);

        float4 blendingOffset = h ? float4(a.x, 0.0, a.z, 0.0) : float4(0.0, a.y, 0.0, a.w);
        float2 blendingWeight = h ? a.xz : a.yw;
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        // Positive offset blends toward right/below, negative toward left/above.
        float4 blendingCoord = uv.xyxy + blendingOffset * float4(tsize.x, tsize.y, -tsize.x, -tsize.y);

        float3 color0 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.xy, 0).rgb;
        float3 color1 = rdgTexturesFloat4[pc.colorIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], blendingCoord.zw, 0).rgb;

        result = blendingWeight.x * color0 + blendingWeight.y * color1;
    }

    rdgStorageFloat4[pc.outputIndex][coord] = float4(result, 1.0);
}
