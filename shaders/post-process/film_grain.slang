import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

float FilmGrainHash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

[shader("compute")]
[numthreads(POST_PROCESS_FILM_GRAIN_DISPATCH_X, POST_PROCESS_FILM_GRAIN_DISPATCH_Y, 1)]
void ComputeFilmGrain(uniform FilmGrainPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) return;
    uint2 pixelCoord = dtid.xy;

    float2 uv = (dtid.xy + 0.5) / pc.outputExtent;

    float2 grainUV = uv * pc.outputExtent / pc.grainSize;
    grainUV += float2(pc.frameIndex * 0.123, pc.frameIndex * 0.456);

    float noise = FilmGrainHash(grainUV);
    noise = noise * 2.0 - 1.0;

    float3 color = rdgTexturesFloat4[pc.inputIndex].Load(int3(pixelCoord, 0)).rgb;
    color += noise * pc.grainStrength;
    rdgStorageFloat4[pc.outputIndex][pixelCoord] = float4(color, 1.0);
}
