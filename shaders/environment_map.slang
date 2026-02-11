import push_constant_interop;

[[vk::binding(0, 0)]]
SamplerState samplers[];

[[vk::binding(1, 0)]]
Texture2D sourceTextures[];

[[vk::binding(2, 0)]]
TextureCube sourceCubemaps[];

[[vk::binding(3, 0)]]
RWTexture2DArray<float4> targetCubemaps[];

// ==================== Equirectangular to Cubemap ====================
float2 SampleSphericalMap(float3 v) {
    float2 invAtan = float2(0.1591, 0.3183);
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}


[numthreads(16, 16, 1)]
void ComputeEquirectToCubemap(uniform EquirectToCubemapPushConstant pc, uint3 tid : SV_DispatchThreadID) {
    RWTexture2DArray<float4> targetCubemap = targetCubemaps[pc.targetIndex];

    uint3 dims;
    targetCubemap.GetDimensions(dims.x, dims.y, dims.z);

    if (tid.x >= dims.x || tid.y >= dims.y) return;

    int3 storePos = int3(tid.xy, tid.z);
    float2 uv = (float2(storePos.xy) + 0.5) / float2(dims.xy);
    uv = uv * 2.0 - 1.0;

    float3 direction;
    switch (storePos.z) {
        case 0: direction = normalize(float3(1.0, -uv.y, -uv.x)); break;
        case 1: direction = normalize(float3(-1.0, -uv.y, uv.x)); break;
        case 2: direction = normalize(float3(uv.x, 1.0, uv.y)); break;
        case 3: direction = normalize(float3(uv.x, -1.0, -uv.y)); break;
        case 4: direction = normalize(float3(uv.x, -uv.y, 1.0)); break;
        case 5: direction = normalize(float3(-uv.x, -uv.y, -1.0)); break;
        default: direction = float3(0.0); break;
    }

    float2 sphericalUV = SampleSphericalMap(direction);
    sphericalUV = saturate(sphericalUV);

    float4 color = sourceTextures[pc.sourceIndex].SampleLevel(samplers[pc.samplerIndex], sphericalUV, 0);
    targetCubemap[storePos] = color;
}

// ==================== Irradiance Convolution ====================
static const float PI = 3.14159265359;

[numthreads(8, 8, 1)]
void ComputeConvolveDiffuse(uniform ConvolveDiffusePushConstant pc, uint3 tid : SV_DispatchThreadID) {
    RWTexture2DArray<float4> diffuseMap = targetCubemaps[pc.targetIndex];

    uint3 dims;
    diffuseMap.GetDimensions(dims.x, dims.y, dims.z);

    if (tid.x >= dims.x || tid.y >= dims.y) return;

    int3 storePos = int3(tid.xy, tid.z);
    float2 uv = (float2(storePos.xy) + 0.5) / float2(dims.xy);
    uv = uv * 2.0 - 1.0;

    float3 direction;
    switch (storePos.z) {
        case 0: direction = normalize(float3(1.0, -uv.y, -uv.x)); break;
        case 1: direction = normalize(float3(-1.0, -uv.y, uv.x)); break;
        case 2: direction = normalize(float3(uv.x, 1.0, uv.y)); break;
        case 3: direction = normalize(float3(uv.x, -1.0, -uv.y)); break;
        case 4: direction = normalize(float3(uv.x, -uv.y, 1.0)); break;
        case 5: direction = normalize(float3(-uv.x, -uv.y, -1.0)); break;
        default: direction = float3(0.0); break;
    }

    float3 irradiance = float3(0.0);

    float3 up = float3(0.0, 1.0, 0.0);
    if (abs(direction.y) > 0.999) {
        up = float3(0.0, 0.0, 1.0);
    }
    float3 right = normalize(cross(up, direction));
    up = normalize(cross(direction, right));

    float sampleDelta = pc.sampleDelta;
    float nrSamples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * direction;

            irradiance += sourceCubemaps[pc.sourceIndex].SampleLevel(samplers[pc.samplerIndex], sampleVec, 0).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }

    irradiance = PI * irradiance * (1.0 / nrSamples);
    diffuseMap[storePos] = float4(irradiance, 1.0);
}

// ==================== Specular Prefilter ====================
static const float RESOLUTION = 1024.0;

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

[numthreads(8, 8, 1)]
void ComputePrefilterSpecular(uniform PrefilterSpecularPushConstant pc, uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pc.width || tid.y >= pc.height) return;

    RWTexture2DArray<float4> prefilteredMap = targetCubemaps[pc.targetIndex];

    int3 storePos = int3(tid.xy, tid.z);
    float2 uv = (float2(storePos.xy) + 0.5) / float2(pc.width, pc.height);
    uv = uv * 2.0 - 1.0;

    float3 direction;
    switch (storePos.z) {
        case 0: direction = normalize(float3(1.0, -uv.y, -uv.x)); break;
        case 1: direction = normalize(float3(-1.0, -uv.y, uv.x)); break;
        case 2: direction = normalize(float3(uv.x, 1.0, uv.y)); break;
        case 3: direction = normalize(float3(uv.x, -1.0, -uv.y)); break;
        case 4: direction = normalize(float3(uv.x, -uv.y, 1.0)); break;
        case 5: direction = normalize(float3(-uv.x, -uv.y, -1.0)); break;
        default: direction = float3(0.0); break;
    }

    float3 N = direction;
    float3 R = N;
    float3 V = R;
    float roughness = pc.roughness;

    uint SAMPLE_COUNT = pc.sampleCount;
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float D = DistributionGGX(NdotH, roughness);
            float pdf = (D * NdotH / (4.0 * HdotV)) + 0.0001;

            float saTexel = 4.0 * PI / (6.0 * RESOLUTION * RESOLUTION);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

            float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefilteredColor += sourceCubemaps[pc.sourceIndex].SampleLevel(samplers[pc.samplerIndex], L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;

    prefilteredMap[storePos] = float4(prefilteredColor, 1.0);
}