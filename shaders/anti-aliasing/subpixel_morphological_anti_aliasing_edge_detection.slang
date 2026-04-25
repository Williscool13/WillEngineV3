import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import shader_functions;

static const float SMAA_THRESHOLD                      = 0.1;
static const float SMAA_DEPTH_THRESHOLD                = 0.1;
static const float SMAA_LOCAL_CONTRAST_ADAPTATION      = 2.0;

// ============================================================
// Pass 1a: Luma Edge Detection
//
// Requires gamma-corrected (linear) color input.
// Standard choice: good quality, low cost.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void LumaEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord   = int2(threadID.xy);
    float2 tsize = pc.sceneData.texelSize;

    float3 C   = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord,                0)).rgb;
    float3 Cl  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-1,  0), 0)).rgb;
    float3 Ct  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -1), 0)).rgb;
    float3 Cr  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 1,  0), 0)).rgb;
    float3 Cb  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0,  1), 0)).rgb;
    float3 Cll = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-2,  0), 0)).rgb;
    float3 Ctt = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -2), 0)).rgb;

    float L   = Luma(C);
    float Ll  = Luma(Cl);
    float Lt  = Luma(Ct);

    float4 delta;
    delta.xy = abs(L - float2(Ll, Lt));
    float2 edges = step(SMAA_THRESHOLD, delta.xy);

    delta.zw   = abs(L - float2(Luma(Cr), Luma(Cb)));
    float2 maxDelta = max(delta.xy, delta.zw);
    maxDelta = max(maxDelta, abs(float2(Ll - Luma(Cll), Lt - Luma(Ctt))));

    edges *= step(max(maxDelta.x, maxDelta.y), SMAA_LOCAL_CONTRAST_ADAPTATION * delta.xy);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

// ============================================================
// Pass 1b: Color Edge Detection
//
// Catches edges luma misses (e.g. isoluminant color boundaries).
// Higher quality than luma, ~3x more samples.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void ColorEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord   = int2(threadID.xy);
    float2 tsize = pc.sceneData.texelSize;

    float3 C  = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord,                0)).rgb;
    float3 Cl = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-1,  0), 0)).rgb;
    float3 Ct = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -1), 0)).rgb;

    float4 delta;
    float3 t;

    t = abs(C - Cl); delta.x = max(max(t.r, t.g), t.b);
    t = abs(C - Ct); delta.y = max(max(t.r, t.g), t.b);

    float2 edges = step(SMAA_THRESHOLD, delta.xy);

    float3 Cr = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 1,  0), 0)).rgb;
    float3 Cb = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0,  1), 0)).rgb;

    t = abs(C - Cr); delta.z = max(max(t.r, t.g), t.b);
    t = abs(C - Cb); delta.w = max(max(t.r, t.g), t.b);

    float2 maxDelta = max(delta.xy, delta.zw);

    float3 Cll = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2(-2,  0), 0)).rgb;
    float3 Ctt = rdgTexturesFloat4[pc.colorIndex].Load(int3(coord + int2( 0, -2), 0)).rgb;

    t = abs(Cl - Cll); delta.z = max(max(t.r, t.g), t.b);
    t = abs(Ct - Ctt); delta.w = max(max(t.r, t.g), t.b);

    maxDelta = max(maxDelta, delta.zw);

    edges *= step(max(maxDelta.x, maxDelta.y), SMAA_LOCAL_CONTRAST_ADAPTATION * delta.xy);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}

// ============================================================
// Pass 1c: Depth Edge Detection
//
// Geometry edges only. Cheapest option, misses texture/color edges.
// ============================================================
[shader("compute")]
[numthreads(16, 16, 1)]
void DepthEdgeDetectionMain(uniform SmaaEdgeDetectionPushConstant pc, uint3 threadID : SV_DispatchThreadID)
{
    if (threadID.x >= uint(pc.sceneData.mainRenderTargetSize.x) || threadID.y >= uint(pc.sceneData.mainRenderTargetSize.y)) { return; }

    int2 coord = int2(threadID.xy);

    float D   = rdgTexturesFloat[pc.depthIndex].Load(int3(coord,                0));
    float Dl  = rdgTexturesFloat[pc.depthIndex].Load(int3(coord + int2(-1,  0), 0));
    float Dt  = rdgTexturesFloat[pc.depthIndex].Load(int3(coord + int2( 0, -1), 0));

    float2 delta = abs(D - float2(Dl, Dt));
    float2 edges = step(SMAA_DEPTH_THRESHOLD, delta);

    rdgStorageFloat2[pc.outputEdgeIndex][coord] = edges;
}
