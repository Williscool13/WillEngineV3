import common_interop;
import model_interop;
import push_constant_interop;
import gpu_debug_draw;

// =============== Shader Summary ============
// GPU debug draw support: converts the appended segment count into mesh-task indirect args, plus a test pattern producer (reference for real producers).

[shader("compute")]
[numthreads(1, 1, 1)]
void ComputeGPUDebugBuildIndirect(uniform GPUDebugBuildIndirectPushConstant pc)
{
    uint count = min(pc.args->segmentCount, pc.args->capacity);
    pc.args->segmentCount = count;
    pc.args->groupCountX = (count + 31) / 32;
    pc.args->groupCountY = 1;
    pc.args->groupCountZ = 1;

    pc.sphereArgs->instanceCount = min(pc.sphereArgs->instanceCount, pc.sphereArgs->capacity);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeGPUDebugTestPattern(uint3 dtid: SV_DispatchThreadID, uniform GPUDebugTestPatternPushConstant pc)
{
    float phase = float(pc.frameIndex) * 0.03 + float(dtid.x + dtid.y) * 0.4;
    float3 center = float3((float(dtid.x) - 3.5) * 3.0, 2.0 + sin(phase), (float(dtid.y) - 3.5) * 3.0);
    float4 color = float4(float(dtid.x) / 7.0, float(dtid.y) / 7.0, 1.0 - float(dtid.x) / 7.0, 1.0);
    GPUDebugAddAABB(pc.args, pc.segmentBuffer, center - 0.5, center + 0.5, 0.02, color);

    float3 axis = normalize(float3(cos(phase), 0.4, sin(phase))) * 0.5;
    GPUDebugAddSphere(pc.sphereArgs, pc.sphereBuffer, center + float3(0.0, 1.6, 0.0), 0.35, float4(0.5, axis), float4(0.5, -axis), float4(0.2, 0.0, 0.0, 0.0));
}
