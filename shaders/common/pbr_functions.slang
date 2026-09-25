import constants_interop;
import model_interop;
import bindless_declarations;

import shader_constants;
module pbr_functions;


public static const bool LIGHT_INVERSE_SQUARE = true;
public static const float LIGHT_GEOM_MIN_DIST2 = 0.1;

public void SampleMetalRoughLod(MaterialProperties material, float2 uv, float lod, out float metallic, out float roughness)
{
    metallic = material.metalRoughFactors.x;
    roughness = material.metalRoughFactors.y;
    if (material.textureImageIndices.y >= 0) {
        float2 mrUv = uv * material.metalRoughUvTransform.xy + material.metalRoughUvTransform.zw;
        float4 mr = assetTextures[NonUniformResourceIndex(material.textureImageIndices.y)].SampleLevel(assetSamplers[NonUniformResourceIndex(material.textureSamplerIndices.y)], mrUv, lod);
        roughness *= mr.g;
        metallic *= mr.b;
    }
}

/** Full attenuation: inverse-square times windowed range cull; use when the path has no separate geometry term. */
public float LightDistanceFalloff(float dist, float range)
{
    if (LIGHT_INVERSE_SQUARE) {
        float distSq = dist * dist;
        float ratioSq = distSq / max(range * range, 1e-8);
        float falloff = saturate(1.0 - ratioSq * ratioSq);
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
        float ratioSq = distSq / max(range * range, 1e-8);
        float falloff = saturate(1.0 - ratioSq * ratioSq);
        return falloff * falloff;
    }

    float a = 1.0 - smoothstep(0.0, range, dist);
    return a * a;
}

/** cosInner == cosOuter == 0 is identity, so every light type can apply it unconditionally. */
public float LightConeFalloff(float cosLight, float cosInner, float cosOuter)
{
    float t = saturate((cosLight - cosOuter) / max(cosInner - cosOuter, 1e-4));
    return t * t * (3.0 - 2.0 * t);
}

public float2 ConcentricSampleDisk(float u1, float u2)
{
    float2 o = 2.0 * float2(u1, u2) - 1.0;
    if (o.x == 0.0 && o.y == 0.0) { return float2(0.0); }
    float r;
    float theta;
    if (abs(o.x) > abs(o.y))
    {
        r = o.x;
        theta = (PI / 4.0) * (o.y / o.x);
    }
    else
    {
        r = o.y;
        theta = (PI / 2.0) - (PI / 4.0) * (o.x / o.y);
    }
    return r * float2(cos(theta), sin(theta));
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

public float D_Beckmann(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float NdotH4 = NdotH2 * NdotH2;
    return exp((NdotH2 - 1.0f) / max(a2 * NdotH2, 1e-6f)) / max(PI * a2 * NdotH4, 1e-6f);
}

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

public float G_SCHLICKGGX_SMITH(float3 N, float3 V, float3 L, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    float NDotV = max(dot(N, V), 0.0f);
    float NDotL = max(dot(N, L), 0.0f);
    return G_SCHLICKGGX(NDotV, k) * G_SCHLICKGGX(NDotL, k);
}

/** Returns G2, same denominator convention as G_SCHLICKGGX_SMITH. */
public float G_SMITH_HEIGHT_CORRELATED(float3 N, float3 V, float3 L, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 2.0f * NdotL * NdotV / max(lambdaV + lambdaL, 1e-6f);
}

/** Returns G for the standard Cook-Torrance denominator. */
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

public float3 F_SCHLICK(float3 V, float3 H, float3 F0) {
    return F0 + (1.0f - F0) * pow(2.0f, FRESNEL_POWER_UNREAL(V, H));
}

public float3 F_SCHLICK_ROUGHNESS(float NdotV, float3 F0, float roughness) {
    float3 F90 = max(float3(1.0f - roughness), F0);
    float Fc = pow(2.0f, (-5.55473f * NdotV - 6.98316f) * NdotV);
    return F0 + (F90 - F0) * Fc;
}


// ==================== Diffuse BRDF Models ====================

/** PI division included. */
public float Diffuse_Lambert() {
    return 1.0f / PI;
}

/** Divides by max(NdotL, NdotV) so it stays bounded at silhouettes. PI division included. */
public float Diffuse_OrenNayar(float NdotL, float NdotV, float3 L, float3 V, float roughness) {
    float a = roughness * roughness;
    float A = 1.0f - 0.5f * a / (a + 0.33f);
    float B = 0.45f * a / (a + 0.09f);
    float s = dot(L, V) - NdotL * NdotV;
    float t = (s <= 0.0f) ? 1.0f : max(max(NdotL, NdotV), 1e-4f);
    return (A + B * s / t) / PI;
}

/** Cosine-weighted hemispherical integral of Diffuse_OrenNayar, the factor on albedo * E/pi under uniform radiance. */
public float OrenNayarDirectionalAlbedo(float NdotV, float roughness) {
    float a = roughness * roughness;
    float A = 1.0f - 0.5f * a / (a + 0.33f);
    float B = 0.45f * a / (a + 0.09f);
    float c = clamp(NdotV, 1e-4f, 1.0f);
    float thetaV = acos(c);
    float sinV = sqrt(max(1.0f - c * c, 0.0f));
    float sin3 = sinV * sinV * sinV;
    float inner = 0.5f * thetaV - 0.5f * sinV * c - sin3 / 3.0f;
    float outer = (1.0f / c - 1.0f) * (1.0f - sin3) / 3.0f;
    return A + 2.0f * B * sinV * (inner + outer) / PI;
}

/** PI division included. */
public float Diffuse_Disney(float NdotL, float NdotV, float VdotH, float roughness) {
    float FD90 = 0.5f + 2.0f * roughness * VdotH * VdotH;
    float FdV = 1.0f + (FD90 - 1.0f) * pow(1.0f - NdotV, 5.0f);
    float FdL = 1.0f + (FD90 - 1.0f) * pow(1.0f - NdotL, 5.0f);
    return FdV * FdL / PI;
}

/** Single-scatter, PI included; add albedo*0.1159*roughness at the call site for multi-scatter. */
public float Diffuse_Hammon(float NdotL, float NdotV, float NdotH, float LdotV, float roughness) {
    float facing = 0.5f + 0.5f * LdotV;
    float rough = facing * (0.9f - 0.4f * facing) * ((0.5f + NdotH) / max(NdotH, 0.05f));
    float smooth = 1.05f * (1.0f - pow(1.0f - NdotL, 5.0f)) * (1.0f - pow(1.0f - NdotV, 5.0f));
    return lerp(smooth, rough, roughness) / PI;
}


/** Engine world space is +Y up; the prefiltered cubemaps are authored Y-flipped, so every cubemap fetch routes its world direction through here. */
public float3 CubemapDirection(float3 worldDir)
{
    return float3(worldDir.x, -worldDir.y, worldDir.z);
}

public float3 DiffuseIrradiance(TextureCube envMap, SamplerState linearSampler, float3 worldNormal)
{
    return envMap.SampleLevel(linearSampler, CubemapDirection(worldNormal), ENVIRONMENT_MAP_DIFFUSE_MIP).rgb;
}

/** Variant for callers that already sampled the BRDF LUT at (NoV, roughness). */
public float3 SpecularReflection(TextureCube envMap, SamplerState linearSampler,
                          float3 viewV, float3 viewN, float4x4 invView, float roughness, float3 F, float2 envBRDF)
{
    float3 R = reflect(-viewV, viewN);
    float3 worldR = normalize(mul((float3x3)invView, R));

    // Mips are prefiltered at squared-spaced roughness ((m/(MIPS-1))^2), so the lookup inverts with sqrt
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, CubemapDirection(worldR), sqrt(roughness) * float(ENVIRONMENT_MAP_SPECULAR_MIPS - 1)).rgb;
    return prefilteredColor * (F * envBRDF.x + envBRDF.y);
}

public float3 SpecularReflection(TextureCube envMap, Texture2D lut, SamplerState linearSampler,
                          float3 viewV, float3 viewN, float4x4 invView, float roughness, float3 F)
{
    float2 brdfUV = float2(max(dot(viewN, viewV), 0.0), roughness);
    float2 envBRDF = lut.SampleLevel(linearSampler, brdfUV, 0).rg;
    return SpecularReflection(envMap, linearSampler, viewV, viewN, invView, roughness, F, envBRDF);
}
