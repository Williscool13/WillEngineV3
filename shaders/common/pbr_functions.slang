import constants_interop;

import shader_constants;
module pbr_functions;


public static const bool LIGHT_INVERSE_SQUARE = true;
public static const float LIGHT_GEOM_MIN_DIST2 = 0.1;

/** Full attenuation: inverse-square times windowed range cull; use when the path has no separate geometry term. */
public float LightDistanceFalloff(float dist, float range)
{
    if (LIGHT_INVERSE_SQUARE) {
        float distSq = dist * dist;
        float rangeInvSq = 1.0 / max(range * range, 1e-8);
        float falloff = saturate(1.0 - distSq * rangeInvSq);
        return (1.0 / (distSq + 1.0)) * falloff * falloff;
    }

    float a = 1.0 - smoothstep(0.0, range, dist);
    return a * a;
}

/** Windowed range cull only (no inverse-square); for paths that already apply LightGeometryTerm's 1/d^2. */
public float LightRangeCull(float dist, float range)
{
    if (LIGHT_INVERSE_SQUARE) {
        float distSq = dist * dist;
        float rangeInvSq = 1.0 / max(range * range, 1e-8);
        float falloff = saturate(1.0 - distSq * rangeInvSq);
        return falloff * falloff;
    }

    float a = 1.0 - smoothstep(0.0, range, dist);
    return a * a;
}

public float LightGeometryTerm(float cosLight, float dist)
{
    if (LIGHT_INVERSE_SQUARE)
    {
        return cosLight / max(dist * dist, LIGHT_GEOM_MIN_DIST2);
    }
    return cosLight;
}

// ==================== Normal Distribution Functions (D) ====================

public float D_GGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

/** Beckmann NDF: predecessor to GGX, Gaussian-shaped specular lobe, slightly different rolloff. */
public float D_Beckmann(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float NdotH4 = NdotH2 * NdotH2;
    return exp((NdotH2 - 1.0f) / max(a2 * NdotH2, 1e-6f)) / max(PI * a2 * NdotH4, 1e-6f);
}

/** Energy-conserving Blinn-Phong NDF; cheaper than GGX with a less natural tail. Shininess from the Beckmann-to-Phong mapping n = 2/alpha^2 - 2. */
public float D_BlinnPhong(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float n = max(2.0f / a2 - 2.0f, 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    return (n + 2.0f) / (2.0f * PI) * pow(NdotH, n);
}


// ==================== Geometry / Shadowing-Masking Functions (G) ====================

float G_SCHLICKGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0f - k) + k);
}

/** Separable Schlick-GGX Smith with Disney "reduced hotness" k remapping (Brian Karis). */
public float G_SCHLICKGGX_SMITH(float3 N, float3 V, float3 L, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    float NDotV = max(dot(N, V), 0.0f);
    float NDotL = max(dot(N, L), 0.0f);
    return G_SCHLICKGGX(NDotV, k) * G_SCHLICKGGX(NDotL, k);
}

/** Height-correlated Smith GGX (Heitz 2014); more physically correct than separable by accounting for masking/shadowing correlation. Returns G2 in [0,1], same denominator as G_SCHLICKGGX_SMITH. */
public float G_SMITH_HEIGHT_CORRELATED(float3 N, float3 V, float3 L, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 2.0f * NdotL * NdotV / max(lambdaV + lambdaL, 1e-6f);
}

/** Kelemen-Szirmay-Kalos G term; very cheap, reduces to 1/(4*HdotV^2) in the specular denominator. Less accurate at grazing angles. Returns G matching the standard Cook-Torrance denominator. */
public float G_KELEMEN(float3 N, float3 V, float3 L, float3 H) {
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float HdotV = max(dot(H, V), 0.0f);
    return (NdotV * NdotL) / max(HdotV * HdotV, 1e-6f);
}


// ==================== Fresnel Functions (F) ====================

float FRESNEL_POWER_UNREAL(float3 V, float3 H) {
    float VDotH = dot(V, H);
    return (-5.55473f * VDotH - 6.98316f) * VDotH;
}

/** Schlick Fresnel with Unreal fast-pow approximation. */
public float3 F_SCHLICK(float3 V, float3 H, float3 F0) {
    return F0 + (1.0f - F0) * pow(2.0f, FRESNEL_POWER_UNREAL(V, H));
}

/** Schlick Fresnel attenuated by roughness for IBL ambient evaluation. */
public float3 F_SCHLICK_ROUGHNESS(float NdotV, float3 F0, float roughness) {
    float3 F90 = max(float3(1.0f - roughness), F0);
    float Fc = pow(2.0f, (-5.55473f * NdotV - 6.98316f) * NdotV);
    return F0 + (F90 - F0) * Fc;
}


// ==================== Diffuse BRDF Models ====================

/** Lambertian diffuse; constant albedo/PI lobe. Returns BRDF value (PI division included). */
public float Diffuse_Lambert() {
    return 1.0f / PI;
}

/** Oren-Nayar diffuse (qualitative model, Oren & Nayar 1994); stable formulation that divides by max(NdotL, NdotV) instead of the smaller cosine, so it stays bounded at silhouette edges. Returns BRDF value (PI division included). */
public float Diffuse_OrenNayar(float NdotL, float NdotV, float3 L, float3 V, float roughness) {
    float a = roughness * roughness;
    float A = 1.0f - 0.5f * a / (a + 0.33f);
    float B = 0.45f * a / (a + 0.09f);
    float s = dot(L, V) - NdotL * NdotV;
    float t = (s <= 0.0f) ? 1.0f : max(max(NdotL, NdotV), 1e-4f);
    return (A + B * s / t) / PI;
}

/** Disney diffuse (Burley 2012); Fresnel-based retroreflection peak and grazing darkening. Returns BRDF value (PI division included). @param VdotH dot(V, H) */
public float Diffuse_Disney(float NdotL, float NdotV, float VdotH, float roughness) {
    float FD90 = 0.5f + 2.0f * roughness * VdotH * VdotH;
    float FdV = 1.0f + (FD90 - 1.0f) * pow(1.0f - NdotV, 5.0f);
    float FdL = 1.0f + (FD90 - 1.0f) * pow(1.0f - NdotL, 5.0f);
    return FdV * FdL / PI;
}

/** Hammon diffuse (GDC 2017); energy-conserving Oren-Nayar fit that pairs naturally with GGX+Smith. Returns single-scatter term (PI included); add albedo*0.1159*roughness at call site for multi-scatter. @param LdotV dot(L, V) */
public float Diffuse_Hammon(float NdotL, float NdotV, float NdotH, float LdotV, float roughness) {
    float facing = 0.5f + 0.5f * LdotV;
    float rough = facing * (0.9f - 0.4f * facing) * ((0.5f + NdotH) / max(NdotH, 0.05f));
    float smooth = 1.05f * (1.0f - pow(1.0f - NdotL, 5.0f)) * (1.0f - pow(1.0f - NdotV, 5.0f));
    return lerp(smooth, rough, roughness) / PI;
}


/** Prefiltered diffuse irradiance lookup; flips Y to convert engine world space to cubemap space. */
public float3 DiffuseIrradiance(TextureCube envMap, SamplerState linearSampler, float3 worldNormal)
{
    float3 N = worldNormal;
    N.y = -N.y;
    return envMap.SampleLevel(linearSampler, N, ENVIRONMENT_MAP_DIFFUSE_MIP).rgb;
}

/** Variant for callers that already sampled the BRDF LUT at (NoV, roughness). */
public float3 SpecularReflection(TextureCube envMap, SamplerState linearSampler,
                          float3 viewV, float3 viewN, float4x4 invView, float roughness, float3 F, float2 envBRDF)
{
    float3 R = reflect(-viewV, viewN);
    float3 worldR = normalize(mul((float3x3)invView, R));
    worldR.y = -worldR.y;

    float3 prefilteredColor = envMap.SampleLevel(linearSampler, worldR, roughness * roughness * float(ENVIRONMENT_MAP_SPECULAR_MIPS - 1)).rgb;
    return prefilteredColor * (F * envBRDF.x + envBRDF.y);
}

public float3 SpecularReflection(TextureCube envMap, Texture2D lut, SamplerState linearSampler,
                          float3 viewV, float3 viewN, float4x4 invView, float roughness, float3 F)
{
    float2 brdfUV = float2(max(dot(viewN, viewV), 0.0), roughness);
    float2 envBRDF = lut.SampleLevel(linearSampler, brdfUV, 0).rg;
    return SpecularReflection(envMap, linearSampler, viewV, viewN, invView, roughness, F, envBRDF);
}
