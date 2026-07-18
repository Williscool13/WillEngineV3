import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

[shader("compute")]
[numthreads(POST_PROCESS_COLOR_GRADING_DISPATCH_X, POST_PROCESS_COLOR_GRADING_DISPATCH_Y, 1)]
void ComputeColorGrading(uniform ColorGradingPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }

    uint2 pixelCoord = dtid.xy;
    float3 color = rdgTexturesFloat4[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;

    color *= exp2(pc.exposure);

    color = (color - 0.5) * pc.contrast + 0.5;

    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma, luma, luma), color, pc.saturation);

    if (pc.temperature > 0.0) {
        color.r += pc.temperature * 0.1;
        color.b -= pc.temperature * 0.1;
    } else {
        color.r += pc.temperature * 0.1;
        color.b -= pc.temperature * 0.1;
    }

    if (pc.tint > 0.0) {
        color.g -= pc.tint * 0.1;
    } else {
        color.g -= pc.tint * 0.1;
    }

    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
