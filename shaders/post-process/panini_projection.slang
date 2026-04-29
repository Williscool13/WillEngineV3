import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

// Ref: http://tksharpless.net/vedutismo/Pannini/panini.pdf and https://www.shadertoy.com/view/Wt3fzB
// Remaps screen UVs to sample from a rectilinear source image with panini distortion.
// strength (d) in [0,1]: 0 = identity (rectilinear), 1 = full stereographic cylinder.
// Typical game values: 0.15 - 0.35.

[shader("compute")]
[numthreads(POST_PROCESS_PANINI_DISPATCH_X, POST_PROCESS_PANINI_DISPATCH_Y, 1)]
void ComputePaniniProjection(uniform PaniniProjectionPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }

    float2 uv = (dtid.xy + 0.5f) / float2(pc.outputExtent);
    float d = pc.strength;
    float d2 = d * d;

    // Map to normalized coords: x in [-1,1], y in [-1/aspect, 1/aspect]
    // Panini math operates in horizontal angular space; vertical is scaled by aspect.
    float2 tc = float2(uv.x * 2.0f - 1.0f, (uv.y * 2.0f - 1.0f) / pc.aspect);

    // Panini b-factor: scales tc so the projection covers exactly the source FOV.
    // Uses horizontal FOV (derived from vertical + aspect) since panini is a cylindrical projection.
    float horizontalFov = 2.0f * atan(tan(pc.perspectiveFov * 0.5f) * pc.aspect);
    float halfFovComplement = PI * 0.5f - horizontalFov * 0.5f;
    float f = cos(halfFovComplement) / sin(halfFovComplement); // cot(pi/2 - hfov/2) = tan(hfov/2)
    float f2 = f * f;
    float b = (sqrt(max(0.0f, (d + d2) * (d + d2) * (f2 + f2 * f2))) - (d * f + f)) / (d2 + d2 * f2 - 1.0f);
    tc *= b;

    // Panini inverse projection: screen point -> view direction
    // Solve for the azimuth angle (phi) on the cylindrical projection surface.
    float h2 = tc.x * tc.x;
    float k = h2 / ((d + 1.0f) * (d + 1.0f));
    float k2 = k * k;
    float discriminant = k2 * d2 - (k + 1.0f) * (k * d2 - 1.0f);

    float3 color = float3(0, 0, 0);
    if (discriminant >= 0.0f) {
        float cosPhi = (-k * d + sqrt(discriminant)) / (k + 1.0f);
        float S = (d + 1.0f) / (d + cosPhi);
        float tanTheta = tc.y / S;

        // Reconstruct view direction directly (no trig round-trip)
        float sinPhi = sqrt(max(0.0f, 1.0f - cosPhi * cosPhi));
        if (tc.x < 0.0f) { sinPhi = -sinPhi; }
        float cosTheta = rsqrt(1.0f + tanTheta * tanTheta);

        float3 viewDir = float3(sinPhi * cosTheta, tanTheta * cosTheta, -cosPhi * cosTheta);

        // Reproject onto the source rectilinear image
        float verticalFocalLength = 1.0f / tan(pc.perspectiveFov * 0.5f);
        float2 ndc = float2(viewDir.x / -viewDir.z / pc.aspect,
                            viewDir.y / -viewDir.z) * verticalFocalLength;
        float2 sampleUV = ndc * 0.5f + 0.5f;

        if (all(sampleUV >= 0.0f) && all(sampleUV <= 1.0f)) {
            color = rdgTexturesFloat4[pc.inputIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], sampleUV, 0).rgb;
        }
    }

    rdgStorageFloat4[pc.outputIndex][dtid.xy] = float4(color, 1.0f);
}
