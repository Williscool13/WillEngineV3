import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;
import tonemap_common;

import bindless_declarations;

// Fused finalize: Panini -> Chromatic Aberration -> Bloom Composite -> Auto Exposure -> Tone-map -> White Balance / Contrast / Saturation -> Vignette
[shader("compute")]
[numthreads(POST_PROCESS_FINALIZE_DISPATCH_X, POST_PROCESS_FINALIZE_DISPATCH_Y, 1)]
void ComputeFinalize(uniform PostProcessFinalizePushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= pc.outputExtent)) { return; }
    uint2 pixelCoord = dtid.xy;

    float2 uv = (float2(pixelCoord) + 0.5) / float2(pc.outputExtent);
    float aspect = pc.aspect;

    // Panini-Vedutismo inverse remap (screen point -> rectilinear source UV)
    // Ref: http://tksharpless.net/vedutismo/Pannini/panini.pdf
    float2 srcUv = uv;
    if (pc.paniniStrength > 0.0) {
        float d = pc.paniniStrength;
        float d2 = d * d;
        float2 tc = float2(uv.x * 2.0 - 1.0, (uv.y * 2.0 - 1.0) / aspect);
        tc *= pc.paniniB;

        float h2 = tc.x * tc.x;
        float k = h2 / ((d + 1.0) * (d + 1.0));
        float k2 = k * k;
        float discriminant = k2 * d2 - (k + 1.0) * (k * d2 - 1.0);

        bool valid = discriminant >= 0.0;
        if (valid) {
            float cosPhi = (-k * d + sqrt(discriminant)) / (k + 1.0);
            float S = (d + 1.0) / (d + cosPhi);
            float tanTheta = tc.y / S;

            float sinPhi = sqrt(max(0.0, 1.0 - cosPhi * cosPhi));
            if (tc.x < 0.0) { sinPhi = -sinPhi; }
            float cosTheta = rsqrt(1.0 + tanTheta * tanTheta);

            float3 viewDir = float3(sinPhi * cosTheta, tanTheta * cosTheta, -cosPhi * cosTheta);
            float2 ndc = float2(viewDir.x / -viewDir.z / aspect, viewDir.y / -viewDir.z) * pc.paniniVerticalFocalLength;
            srcUv = ndc * 0.5 + 0.5;
            valid = all(srcUv >= 0.0) && all(srcUv <= 1.0);
        }
        if (!valid) {
            rdgStorageFloat4[pc.dstImageIndex][pixelCoord] = float4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    float3 sceneColor = float3(0.0, 0.0, 0.0);
    if (pc.chromaticAberrationStrength > 0.0) {
        // Aspect-corrected radius with a pixel-space direction so the R/B separation is uniform in every direction
        float2 co = (uv - 0.5) * float2(aspect, 1.0);
        float radius = length(co);
        float2 uvShift = float2(0.0, 0.0);
        if (radius > 1e-4) {
            float2 dirPix = normalize((uv - 0.5) * float2(pc.outputExtent));
            uvShift = dirPix * (radius * pc.chromaticAberrationStrength) / float2(pc.outputExtent);
        }
        sceneColor.r = rdgTexturesFloat4[pc.srcImageIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], srcUv + uvShift, 0).r;
        sceneColor.g = rdgTexturesFloat4[pc.srcImageIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], srcUv, 0).g;
        sceneColor.b = rdgTexturesFloat4[pc.srcImageIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], srcUv - uvShift, 0).b;
    } else {
        sceneColor = rdgTexturesFloat4[pc.srcImageIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], srcUv, 0).rgb;
    }

    float3 bloom = float3(0.0, 0.0, 0.0);
    if ((pc.flags & POST_PROCESS_FINALIZE_FLAG_BLOOM) != 0) {
        bloom = rdgTexturesFloat4[pc.bloomImageIndex].SampleLevel(rdgSamplers[RDG_LINEAR_SAMPLER_INDEX], srcUv, 0).rgb;
    }
    float3 hdrColor = sceneColor + bloom * pc.bloomIntensity;
    // Last firewall before display
    if (any(isnan(hdrColor))) { hdrColor = float3(0.0, 0.0, 0.0); }
    hdrColor = max(hdrColor, 0.0);

    float exposure = pc.exposureBias;
    if ((pc.flags & POST_PROCESS_FINALIZE_FLAG_EXPOSURE) != 0) {
        float avgLuminance = pc.luminanceBufferAddress[0];
        exposure = pc.targetLuminance / max(avgLuminance, 1e-4) * pc.exposureBias;
    }
    float3 exposedColor = hdrColor * exposure;

    float4 p0 = pc.params0;
    float4 p1 = pc.params1;
    float3 ldrColor;
    switch (pc.tonemapOperator) {
        case 0: ldrColor = ACESFitted(exposedColor); break;
        case 1: ldrColor = HableTonemap(exposedColor, p0.x); break;
        case 2: ldrColor = ReinhardTonemap(exposedColor, p0.x); break;
        case 3: ldrColor = LottesTonemapSimple(exposedColor); break;
        case 4: ldrColor = ReinhardJodie(exposedColor); break;
        case 5: ldrColor = ClampTonemap(exposedColor); break;
        case 6: ldrColor = HejlBurgessDawson(exposedColor); break;
        case 7: ldrColor = UchimuraTonemap(exposedColor, p0.x, p0.y, p0.z, p0.w, p1.x, p1.y); break;
        case 8: ldrColor = ACESNarkowicz(exposedColor); break;
        case 9: ldrColor = AgXTonemap(exposedColor, p0.x, p0.y); break;
        case 10: ldrColor = KhronosPBRNeutral(exposedColor, p0.x, p0.y); break;
        default: ldrColor = saturate(exposedColor); break;
    }

    if ((pc.flags & POST_PROCESS_FINALIZE_FLAG_GRADING) != 0) {
        ldrColor = mul(float3x3(pc.wbRow0.xyz, pc.wbRow1.xyz, pc.wbRow2.xyz), ldrColor);
        if (pc.contrast != 1.0) {
            // Log-space contrast pivoted at mid-gray; cannot produce negatives
            const float pivot = -2.473931; // log2(0.18)
            float3 logColor = log2(max(ldrColor, 1e-5));
            ldrColor = exp2((logColor - pivot) * pc.contrast + pivot);
        }
        float luma = dot(ldrColor, float3(0.2126, 0.7152, 0.0722));
        ldrColor = lerp(float3(luma, luma, luma), ldrColor, pc.saturation);
        ldrColor = saturate(ldrColor);
    }

    if (pc.vignetteStrength > 0.0) {
        // roundness lerps the falloff between screen-fit elliptical and aspect-corrected circular
        float2 vScale = lerp(float2(1.0, 1.0), float2(aspect, 1.0), pc.vignetteRoundness);
        float vDist = length((uv - 0.5) * vScale);
        float s = max(pc.vignetteSmoothness, 1e-4);
        float vignette = 1.0 - smoothstep(pc.vignetteRadius - s, pc.vignetteRadius, vDist);
        ldrColor *= lerp(1.0, vignette, pc.vignetteStrength);
    }

    rdgStorageFloat4[pc.dstImageIndex][pixelCoord] = float4(saturate(ldrColor), 1.0);
}
