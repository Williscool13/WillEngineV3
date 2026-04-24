import push_constant_interop;
import constants_interop;

[[vk::binding(0, 0)]] RWTexture2D<float2> storageBuffer[];

static const uint AREA_W     = 160;
static const uint AREA_H     = 560;
static const uint SUBTEX_H   = 80;  // 560 / 7 subtextures

// Analytical blend weight for a horizontal edge run.
// Texture x encodes (e1, sqrtD1): e1 = px>=64, sqrtD1 = px - e1*64
// Texture y (local) encodes (e2, sqrtD2): e2 = py>=64, sqrtD2 = py - e2*64
// R: blend weight contributed by the right-side crossing (e2)
// G: blend weight contributed by the left-side crossing (e1)
float2 ComputeArea(int px, int local_py)
{
    float e1 = (px >= 64) ? 1.0 : 0.0;
    float e2 = (local_py >= 64) ? 1.0 : 0.0;

    float sqrtD1 = float(px       - int(e1) * 64);
    float sqrtD2 = float(local_py - int(e2) * 64);
    float d1     = sqrtD1 * sqrtD1;
    float d2     = sqrtD2 * sqrtD2;
    float total  = d1 + d2 + 1.0;

    float R = e2 * 0.5 * (d1 + 0.5) / total;
    float G = e1 * 0.5 * (d2 + 0.5) / total;
    return float2(R, G);
}

[numthreads(SMAA_AREA_GENERATION_DISPATCH_X, SMAA_AREA_GENERATION_DISPATCH_Y, 1)]
void GenerateSMAAArea(uniform SMAAAreaGeneratePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= AREA_W || tid.y >= AREA_H) { return; }
    int local_py = int(tid.y) % int(SUBTEX_H);
    float2 area  = ComputeArea(int(tid.x), local_py);
    storageBuffer[pc.targetIndex][tid.xy] = area;
}
