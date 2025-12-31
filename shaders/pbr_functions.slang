module pbr_functions;


public float D_GGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (3.14159265359 * denom * denom);
}

public float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

public float G_Smith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = G_SchlickGGX(NdotV, roughness);
    float ggx2 = G_SchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}


float G_SCHLICKGGX(float NdotX, float k){
    float num = NdotX;
    float denom = NdotX * (1.0f - k) + k;
    return num / denom;
}

public float G_SCHLICKGGX_SMITH(float3 N, float3 V, float3 L, float roughness){
    // height correlated smith method

    // "Disney reduced hotness" - Brian Karis
    float r = (roughness + 1);
    float k = (r * r) / 8.0f;

    float NDotV = max(dot(N, V), 0.0f);
    float NDotL = max(dot(N, L), 0.0f);

    float ggx2 = G_SCHLICKGGX(NDotV, k);
    float ggx1 = G_SCHLICKGGX(NDotL, k);
    return ggx1 * ggx2;
}

public float3 F_Schlick(float3 V, float3 H, float3 F0) {
    float cosTheta = max(dot(V, H), 0.0);
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}