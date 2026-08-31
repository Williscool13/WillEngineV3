import push_constant_interop;
import lights_interop;
import light_index_common;
import shader_constants;
import packing_functions;

groupshared float gScan[256];

static float LightPower(LightInfo L)
{
    float area;
    if (L.type == LIGHT_TYPE_SPHERE) {
        float radius = L.right.w;
        area = 4.0 * PI * radius * radius;
    } else if (L.type == LIGHT_TYPE_TRIANGLE) {
        area = 0.5 * length(cross(L.right.xyz, L.up.xyz));
    } else {
        area = 4.0 * L.right.w * L.up.w;
    }
    float3 color = UnpackUnorm4x8(L.packedColor).rgb;
    float power = L.intensity * area * max(color.r, max(color.g, color.b));
    return max(power, 0.0);
}

// Dispatched as a single workgroup; runningTotal chains the 256-wide chunks sequentially.
[shader("compute")]
[numthreads(256, 1, 1)]
void ComputeLightPowerCDF(uniform LightPowerCDFPushConstant pc, uint3 localID : SV_GroupThreadID)
{
    uint tid = localID.x;
    float runningTotal = 0.0;
    for (uint base = 0u; base < pc.liveCount; base += 256u) {
        uint i = base + tid;
        float p = 0.0;
        if (i < pc.liveCount) {
            p = LightPower(pc.lightData->lights[LiveLightIndex(i, pc.analyticCount)]);
        }
        gScan[tid] = p;
        GroupMemoryBarrierWithGroupSync();
        for (uint offset = 1u; offset < 256u; offset <<= 1u) {
            float v = tid >= offset ? gScan[tid - offset] : 0.0;
            GroupMemoryBarrierWithGroupSync();
            gScan[tid] += v;
            GroupMemoryBarrierWithGroupSync();
        }
        if (i < pc.liveCount) {
            pc.cdf[i] = runningTotal + gScan[tid];
        }
        runningTotal += gScan[255];
        GroupMemoryBarrierWithGroupSync();
    }
}
