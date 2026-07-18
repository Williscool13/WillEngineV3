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

public float3 ACESFitted(float3 color) {
    color = mul(ACESInputMat, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMat, color);
    return saturate(color);
}

// ============================================================
// ACES (Narkowicz approximation)
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// ============================================================
public float3 ACESNarkowicz(float3 x) {
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

public float3 HableTonemap(float3 x, float whitePoint) {
    return HablePartial(x * 2.0) / HablePartial(float3(whitePoint, whitePoint, whitePoint));
}

// ============================================================
// Reinhard (extended with white point)
// ============================================================
public float3 ReinhardTonemap(float3 x, float whitePoint) {
    float w2 = whitePoint * whitePoint;
    return (x * (1.0 + x / w2)) / (1.0 + x);
}

// ============================================================
// Reinhard-Jodie
// https://www.shadertoy.com/view/4dBcD1 (Jodie)
// ============================================================
public float3 ReinhardJodie(float3 v) {
    float l = dot(v, float3(0.2126, 0.7152, 0.0722));
    float3 tv = v / (1.0 + v);
    return lerp(v / (1.0 + l), tv, tv);
}

// ============================================================
// Lottes 2016 (optimized Lottes)
// https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
// ============================================================
public float3 LottesTonemapSimple(float3 c) {
    return c / (max(max(c.r, c.g), c.b) + 1.0);
}

// ============================================================
// Clamp (baseline; no compression, just clip)
// ============================================================
public float3 ClampTonemap(float3 x) {
    return saturate(x);
}

// ============================================================
// Hejl-Burgess-Dawson
// GDC 2010 "Filmic Tonemapping for Real-time Rendering"
// The curve bakes gamma 2.2 in; linearized on the way out so the operator honors the chain's linear-output contract.
// ============================================================
public float3 HejlBurgessDawson(float3 x) {
    x = max(float3(0.0, 0.0, 0.0), x - 0.004);
    float3 encoded = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return pow(encoded, 2.2);
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

public float3 UchimuraTonemap(float3 x, float P, float a, float m, float l, float c, float b) {
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

// Row-major forms of the GLSL column-major AgX inset/outset matrices
static const float3x3 AgXInsetMat = {
    {0.842479062253094,  0.0784335999999992, 0.0792237451477643},
    {0.0423282422610123, 0.878468636469772,  0.0791661274605434},
    {0.0423756549057051, 0.0784336,          0.879142973793104 }
};

static const float3x3 AgXOutsetMat = {
    { 1.19687900512017,   -0.0980208811401368, -0.0990297440797205},
    {-0.0528968517574562,  1.15190312990417,   -0.0989611768448433},
    {-0.0529716355144438, -0.0980434501171241,  1.15107367264116  }
};

float3 AgXContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  15.5     * x4 * x2
           - 40.14   * x4 * x
           + 31.96   * x4
           - 6.868   * x2 * x
           + 0.4298  * x2
           + 0.1191  * x
           - 0.00232;
}

public float3 AgXTonemap(float3 val, float minEV, float maxEV) {
    val = mul(AgXInsetMat, val);
    val = clamp(log2(max(val, 1e-10)), minEV, maxEV);
    val = (val - minEV) / (maxEV - minEV);
    val = AgXContrastApprox(val);
    // Outset (inverse inset) then linearize: the sigmoid output is 2.2-gamma encoded
    val = mul(AgXOutsetMat, val);
    return pow(max(val, 0.0), 2.2);
}

// ============================================================
// Khronos PBR Neutral
// https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral
// ============================================================
public float3 KhronosPBRNeutral(float3 color, float startCompression, float desaturation) {

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
