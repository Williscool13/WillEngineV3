import common_interop;
import push_constant_interop;
import bindless_declarations;
import constants_interop;

// 3x3 box filter atrous for gradient spreading and upsampling.
// Uses UV-based sampling so the input texture can be any resolution
// (e.g. strided for the upsample pass, or full-res for spread passes).
[shader("compute")]
[numthreads(8, 8, 1)]
void SVGFGradientAtrousMain(uniform SVGFGradientAtrousPushConstant pc, uint3 dtid : SV_DispatchThreadID)
{
    const uint2 pixel = dtid.xy;
    if (pixel.x >= pc.width || pixel.y >= pc.height) {
        return;
    }

    const float2 texelSize = float2(1.0 / float(pc.width), 1.0 / float(pc.height));
    const float2 centerUV = (float2(pixel) + 0.5) * texelSize;

    float sum = 0.0;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            const float2 sampleUV = centerUV + float2(dx, dy) * texelSize * float(pc.stepSize);
            const float2 clampedUV = clamp(sampleUV, float2(0.0), float2(1.0));
            sum += rdgTexturesFloat[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], clampedUV, 0).r;
        }
    }

    rdgStorageFloat[pc.outputIndex][pixel] = sum / 9.0;
}
