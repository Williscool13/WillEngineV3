import push_constant_interop;
import constants_interop;

import shader_functions;
import shader_constants;

import bindless_declarations;

// ============================================================
// ACES Fitted (Stephen Hill / MJP)
// https://github.com/TheRealMJP/BakingLab
// ============================================================
static const float3x3 ACESInputMat = {
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

static const float3x3 ACESOutputMat = {
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ============================================================
// ACES (Narkowicz approximation)
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// ============================================================
float3 ACESNarkowicz(float3 x) {
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return saturate((x * (A * x + B)) / (x * (C * x + D) + E));
}

// ============================================================
// Hable / Uncharted 2
// https://www.gdcvault.com/play/1012351/Uncharted-2-HDR
// ============================================================
float3 HablePartial(float3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 HableTonemap(float3 x, float whitePoint) {
    return HablePartial(x * 2.0) / HablePartial(float3(whitePoint, whitePoint, whitePoint));
}

// ============================================================
// Reinhard (extended with white point)
// ============================================================
float3 ReinhardTonemap(float3 x, float whitePoint) {
    float w2 = whitePoint * whitePoint;
    return (x * (1.0 + x / w2)) / (1.0 + x);
}

// ============================================================
// Reinhard-Jodie
// https://www.shadertoy.com/view/4dBcD1 (Jodie)
// ============================================================
float3 ReinhardJodie(float3 v) {
    float l = dot(v, float3(0.2126, 0.7152, 0.0722));
    float3 tv = v / (1.0 + v);
    return lerp(v / (1.0 + l), tv, tv);
}

// ============================================================
// Lottes 2016 (optimized Lottes)
// https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
// ============================================================
float3 LottesTonemapSimple(float3 c) {
    return c / (max(max(c.r, c.g), c.b) + 1.0);
}

// ============================================================
// Clamp (baseline; no compression, just clip)
// ============================================================
float3 ClampTonemap(float3 x) {
    return saturate(x);
}

// ============================================================
// Hejl-Burgess-Dawson
// GDC 2010 "Filmic Tonemapping for Real-time Rendering"
// NOTE: bakes in gamma 2.2 correction; no separate gamma step needed.
// ============================================================
float3 HejlBurgessDawson(float3 x) {
    x = max(float3(0.0, 0.0, 0.0), x - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// ============================================================
// Uchimura (Gran Turismo)
// https://www.desmos.com/calculator/gslcdxvipg
// ============================================================
float UchimuraChannel(float x, float P, float a, float m, float l, float c, float b) {
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;
    float T = m * pow(max(x / m, 0.0), c) + b;
    float S = P - (P - S1) * exp(CP * (x - S0));
    float L = m + a * (x - m);
    return T * w0 + L * w1 + S * w2;
}

float3 UchimuraTonemap(float3 x, float P, float a, float m, float l, float c, float b) {
    return float3(
        UchimuraChannel(x.r, P, a, m, l, c, b),
        UchimuraChannel(x.g, P, a, m, l, c, b),
        UchimuraChannel(x.b, P, a, m, l, c, b)
    );
}

// ============================================================
// AgX (analytic approximation)
// https://iolite-engine.com/blog_posts/minimal_agx_implementation
// ============================================================

// Row-major form of the GLSL column-major AgX inset matrix
static const float3x3 AgXInsetMat = {
    {0.842479062253094,  0.0784335999999992, 0.0792237451477643},
    {0.0423282422610123, 0.878468636469772,  0.0791661274605434},
    {0.0423756549057051, 0.0784336,          0.879142973793104 }
};

float3 AgXContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    float3 x6 = x4 * x2;
    return  15.5     * x6 * x
           - 40.14   * x6
           + 31.96   * x4 * x
           - 6.868   * x4
           + 0.4298  * x2 * x
           + 0.1191  * x2
           - 0.00232 * x;
}

float3 AgXTonemap(float3 val, float minEV, float maxEV) {
    val = mul(AgXInsetMat, val);
    val = clamp(log2(max(val, 1e-10)), minEV, maxEV);
    val = (val - minEV) / (maxEV - minEV);
    return AgXContrastApprox(val);
}

// ============================================================
// Khronos PBR Neutral
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
// ============================================================
float3 KhronosPBRNeutral(float3 color, float startCompression, float desaturation) {

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) { return color; }

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeSDRTonemap(uniform TonemapSDRPushConstant pc, uint3 dtid : SV_DispatchThreadID) {
    int2 pixelCoord = int2(dtid.xy);
    float3 sceneColor = rdgTexturesFloat4[pc.srcImageIndex].Load(int3(pixelCoord, 0)).rgb;

    float3 bloom = float3(0.0, 0.0, 0.0);
    if (pc.bBloomEnabled != 0) {
        bloom = rdgTexturesFloat4[pc.bloomImageIndex].Load(int3(pixelCoord, 0)).rgb;
    }
    float3 hdrColor = sceneColor + bloom * pc.bloomIntensity;

    float exposure = 1.0;
    if (pc.bExposureEnabled != 0) {
        float avgLuminance = pc.luminanceBufferAddress[0];
        exposure = pc.targetLuminance / max(avgLuminance, 0.001);
    }
    float3 exposedColor = hdrColor * exposure;

    float3 ldrColor;
    switch (pc.tonemapOperator) {
        case 0: ldrColor = ACESFitted(exposedColor); break;
        case 1: ldrColor = HableTonemap(exposedColor, pc.params[0]); break;
        case 2: ldrColor = ReinhardTonemap(exposedColor, pc.params[0]); break;
        case 3: ldrColor = LottesTonemapSimple(exposedColor); break;
        case 4: ldrColor = ReinhardJodie(exposedColor); break;
        case 5: ldrColor = ClampTonemap(exposedColor); break;
        case 6: ldrColor = HejlBurgessDawson(exposedColor); break;
        case 7: ldrColor = UchimuraTonemap(exposedColor, pc.params[0], pc.params[1], pc.params[2], pc.params[3], pc.params[4], pc.params[5]); break;
        case 8: ldrColor = ACESNarkowicz(exposedColor); break;
        case 9: ldrColor = AgXTonemap(exposedColor, pc.params[0], pc.params[1]); break;
        case 10: ldrColor = KhronosPBRNeutral(exposedColor, pc.params[0], pc.params[1]); break;
        default: ldrColor = exposedColor;                                                                                         break;
    }

    rdgStorageFloat4[pc.dstImageIndex][pixelCoord] = float4(ldrColor, 1.0);
}
