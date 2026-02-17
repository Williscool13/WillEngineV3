import constants_interop;

module pbr_functions;


public float D_GGX(float3 N, float3 H, float roughness){
    // Brian Karis says GGX NDF has a longer "tail" that appears more natural
    float a = roughness * roughness;
    float a2 = a * a; // disney reparameterization
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float premult = NdotH2 * (a2 - 1.0f) + 1.0f;
    float denom = 3.14159265359 * premult * premult;
    return num / denom;
}

float G_SCHLICKGGX(float NdotX, float k){
    float num = NdotX;
    float denom = NdotX * (1.0f - k) + k;
    return num / denom;
}

public float G_SCHLICKGGX_SMITH(float3 N, float3 V, float3 L, float roughness){
    // height correlated smith method
    // todo: apparently this is not height correlated. Fix

    // "Disney reduced hotness" - Brian Karis
    float r = (roughness + 1);
    float k = (r * r) / 8.0f;

    float NDotV = max(dot(N, V), 0.0f);
    float NDotL = max(dot(N, L), 0.0f);

    float ggx2 = G_SCHLICKGGX(NDotV, k);
    float ggx1 = G_SCHLICKGGX(NDotL, k);
    return ggx1 * ggx2;
}

float FRESNEL_POWER_UNREAL(float3 V, float3 H){
    float VDotH = dot(V, H);
    return (-5.55473 * VDotH - 6.98316) * VDotH;
}

public float3 F_SCHLICK(float3 V, float3 H, float3 F0){
    float VdotH = max(dot(V, H), 0.0f);

    // classic
    //return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    // unreal optimized
    return F0 + (1 - F0) * pow(2, FRESNEL_POWER_UNREAL(V, H));
}

public float3 DiffuseIrradiance(TextureCube envMap, SamplerState linearSampler, float3 worldNormal)
{
    float3 N = worldNormal;
    N.y = -N.y;
    return envMap.SampleLevel(linearSampler, N, ENVIRONMENT_MAP_DIFFUSE_MIP).rgb;
}

public float3 SpecularReflection(TextureCube envMap, Texture2D lut, SamplerState linearSampler,
                          float3 viewV, float3 viewN, float4x4 invView, float roughness, float3 F)
{
    float3 R = reflect(-viewV, viewN);
    float3 worldR = normalize(mul((float3x3)invView, R));
    worldR.y = -worldR.y;

    float3 prefilteredColor = envMap.SampleLevel(linearSampler, worldR, roughness * float(ENVIRONMENT_MAP_SPECULAR_MIPS - 1)).rgb;
    float2 brdfUV = float2(max(dot(viewN, viewV), 0.0), roughness);
    brdfUV.y = 1 - brdfUV.y;
    float2 envBRDF = lut.SampleLevel(linearSampler, brdfUV, 0).rg;

    return prefilteredColor * (F * envBRDF.x + envBRDF.y);
}