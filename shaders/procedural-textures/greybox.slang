import constants_interop;
import push_constant_interop;
import procedural_texture_declarations;

// Measurements are in 512 px tile units, tones are sRGB, linearised on write.
static const float REF_SIZE = 512.0;
static const float FILL_SRGB = 0.64;
static const float GROUT_SRGB = 0.32;
static const float MARK_SRGB = 0.90;
static const float VIGNETTE_SRGB = 0.04;
static const float NOISE_SRGB = 0.015;
static const float GROUT_PX = 4.0;
static const float BRACKET_INSET_PX = 18.0;
static const float BRACKET_ARM_PX = 44.0;
static const float BRACKET_THICK_PX = 3.0;
static const float TICK_LEN_PX = 14.0;
static const float TICK_THICK_PX = 3.0;

float hash(float2 p)
{
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float periodicValueNoise(float2 p, float period)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float2 i0 = fmod(i, period);
    float2 i1 = fmod(i + 1.0, period);
    float a = hash(float2(i0.x, i0.y));
    float b = hash(float2(i1.x, i0.y));
    float c = hash(float2(i0.x, i1.y));
    float d = hash(float2(i1.x, i1.y));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Coverage(float distPx, float halfWidthPx, float aa)
{
    return 1.0 - smoothstep(halfWidthPx - aa, halfWidthPx + aa, distPx);
}

// Axis-aligned box coverage from a signed distance in each axis.
float BoxCoverage(float2 centreDist, float2 halfSize, float aa)
{
    return Coverage(centreDist.x, halfSize.x, aa) * Coverage(centreDist.y, halfSize.y, aa);
}

float SrgbToLinear(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

[shader("compute")]
[numthreads(PROCEDURAL_TEXTURE_DISPATCH_X, PROCEDURAL_TEXTURE_DISPATCH_Y, 1)]
void ComputeGreybox(uniform ProceduralTextureBasePushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 size;
    proceduralOutputTextures[pc.outputIndex].GetDimensions(size.x, size.y);
    if (tid.x >= size.x || tid.y >= size.y) { return; }

    float2 p = (float2(tid.xy) + 0.5) * (REF_SIZE / float2(size));
    float aa = 0.5 * REF_SIZE / float(size.x);
    float2 uv = p / REF_SIZE;

    float2 q = min(p, REF_SIZE - p);

    float noise = periodicValueNoise(uv * 4.0, 4.0) * 0.65 + periodicValueNoise(uv * 16.0, 16.0) * 0.35;
    float2 fromCentre = abs(uv - 0.5) * 2.0;
    float vignette = dot(fromCentre, fromCentre) * 0.5;
    float tone = FILL_SRGB + (noise - 0.5) * 2.0 * NOISE_SRGB - vignette * VIGNETTE_SRGB;

    float2 armOrigin = float2(BRACKET_INSET_PX, BRACKET_INSET_PX);
    float2 armH = float2(BRACKET_ARM_PX, BRACKET_THICK_PX) * 0.5;
    float2 armV = float2(BRACKET_THICK_PX, BRACKET_ARM_PX) * 0.5;
    float bracket = max(BoxCoverage(abs(q - (armOrigin + armH)), armH, aa), BoxCoverage(abs(q - (armOrigin + armV)), armV, aa));

    float2 tickH = float2(TICK_THICK_PX, TICK_LEN_PX) * 0.5;
    float tickTop = BoxCoverage(abs(float2(q.x, q.y) - float2(REF_SIZE * 0.5, GROUT_PX * 0.5 + tickH.y)), tickH, aa);
    float tickSide = BoxCoverage(abs(float2(q.y, q.x) - float2(REF_SIZE * 0.5, GROUT_PX * 0.5 + tickH.y)), tickH, aa);
    float tick = max(tickTop, tickSide);

    tone = lerp(tone, MARK_SRGB, max(bracket, tick));
    tone = lerp(tone, GROUT_SRGB, Coverage(min(q.x, q.y), GROUT_PX * 0.5, aa));

    float albedo = SrgbToLinear(saturate(tone));
    proceduralOutputTextures[pc.outputIndex][tid.xy] = float4(albedo.xxx, 1.0);
}
