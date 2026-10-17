import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw_common;
import lights_interop;

// =============== Shader Summary ============
// Emits each live emissive emitter's (mode 1) or meshlet's (mode 2) range-padded AABB, the box light binning tests cells against, plus its unpadded bounds.

static const float4 RANGE_COLOR = float4(1.0, 0.55, 0.1, 1.0);
static const float4 BOUNDS_COLOR = float4(1.0, 1.0, 1.0, 0.6);

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeGPUDebugEmissiveRange(uint3 tid: SV_DispatchThreadID, uniform EmissiveRangeDebugPushConstant pc)
{
    uint index = tid.x;
    float3 aabbMin;
    float3 aabbMax;
    float pad;
    if (pc.mode == 1u) {
        if (index >= uint(pc.lightData->emissiveMeshCount)) { return; }
        EmissiveMesh mesh = pc.lightData->emissiveMeshes[index];
        if (mesh.power <= 0.0) { return; }
        aabbMin = mesh.aabbMin;
        aabbMax = mesh.aabbMax;
        pad = mesh.rangePad;
    }
    else {
        if (index >= uint(pc.lightData->emissiveMeshletCount)) { return; }
        EmissiveMeshlet meshlet = pc.lightData->emissiveMeshlets[index];
        if (meshlet.power <= 0.0) { return; }
        aabbMin = meshlet.aabbMin;
        aabbMax = meshlet.aabbMax;
        pad = meshlet.rangePad;
    }
    if (any(aabbMin > aabbMax)) { return; }

    GPUDebugAddAABB(pc.args, pc.segmentBuffer, aabbMin, aabbMax, 0.02, RANGE_COLOR);
    GPUDebugAddAABB(pc.args, pc.segmentBuffer, aabbMin + pad, aabbMax - pad, 0.01, BOUNDS_COLOR);
}
