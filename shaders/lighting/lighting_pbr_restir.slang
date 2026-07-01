import common_interop;
import push_constant_interop;
import constants_interop;
import model_interop;
import lights_interop;
import restir_interop;

import shader_functions;
import pbr_functions;
import packing_functions;
import visibility_lighting_common;
import restir_common;

import bindless_declarations;

float3 SanitizeRadiance(float3 c) {
    c.x = (isnan(c.x) || isinf(c.x)) ? 0.0 : c.x;
    c.y = (isnan(c.y) || isinf(c.y)) ? 0.0 : c.y;
    c.z = (isnan(c.z) || isinf(c.z)) ? 0.0 : c.z;
    return clamp(c, 0.0, 65504.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingPbrRestir(uniform VisibilityLightingPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    uint field = pc.activeCheckerboardField;
    if (field != 0u && !IsActiveCheckerboardPixel(int2(pixel), false, field)) {
        return;
    }
    uint2 rcoord = PixelPosToReservoirPos(pixel, field);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = pixel;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, pc.shadowsIndex);
    if (!li.valid) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    float roughness = max(li.roughness, 0.045);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float3 diffuseRadiance = float3(0.0);
    float3 specularRadiance = float3(0.0);
    float hitDistance = 0.0;

    bool bAnyLight = pc.reservoirBuffer != 0;
    if (bAnyLight)
    {
        uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
        uint pixelIdx = rcoord.y * stride + rcoord.x;
        float bestSpecLum = -1.0;

        Reservoir r = pc.reservoirBuffer[pixelIdx];

        if (r.lightIdx == LIGHT_IDX_SUN && r.W > 0.0)
        {
            float3 Lsun = normalize(mul((float3x3)scene.view, UnpackNormalOct16(r.sampleOffsetPacked)));
            float nDotLsun = max(dot(N, Lsun), 0.0);
            if (nDotLsun > 0.0)
            {
                float3 Hsun = normalize(V + Lsun);
                float NDFsun = D_GGX(N, Hsun, roughness);
                float Gsun = G_SMITH_HEIGHT_CORRELATED(N, V, Lsun, roughness);
                float3 Fsun = F_SCHLICK(V, Hsun, F0);
                float3 specularSun = (NDFsun * Gsun * Fsun) / max(4.0 * nDotV * nDotLsun, 0.001);
                float3 kDsun = (1.0 - Fsun) * (1.0 - li.metallic);
                float orenNayarSun = Diffuse_OrenNayar(nDotLsun, nDotV, Lsun, V, roughness);
                float3 diffuseSun = kDsun * li.albedo.xyz * orenNayarSun;
                float3 sunRad = SunRadiance(pc.lightData->directionalLight);
                float3 commonTermSun = nDotLsun * sunRad * r.W;
                diffuseRadiance += diffuseSun * commonTermSun;
                float3 specContributionSun = specularSun * commonTermSun;
                specularRadiance += specContributionSun;
                float specLumSun = dot(specContributionSun, float3(0.2126, 0.7152, 0.0722));
                if (specLumSun > bestSpecLum)
                {
                    bestSpecLum = specLumSun;
                    hitDistance = 10000.0;
                }
            }
        }
        else if (r.lightIdx != ~0u && r.W > 0.0 && r.lightIdx < uint(pc.lightData->lightCount))
        {
            LightVS vs = UnpackLightVS(pc.lightVS[r.lightIdx]);
            float3 yStar = DecodeSamplePos(r, vs);
            LightSample s = EvalLightSample(vs, yStar, viewPosition, N, vs.range);

            if (s.cosShade > 0.0 && s.cosLight > 0.0 && s.attenuation > 0.0)
            {
                float3 Lal = normalize(yStar - viewPosition);
                float3 Hal = normalize(V + Lal);

                float NDFal = D_GGX(N, Hal, roughness);
                float Gal = G_SMITH_HEIGHT_CORRELATED(N, V, Lal, roughness);
                float3 Fal = F_SCHLICK(V, Hal, F0);
                float3 specularAl = (NDFal * Gal * Fal) / max(4.0 * nDotV * max(dot(N, Lal), 0.0), 0.001);

                float3 kSal = Fal;
                float3 kDal = (1.0 - kSal) * (1.0 - li.metallic);
                float orenNayarAl = Diffuse_OrenNayar(s.cosShade, nDotV, Lal, V, roughness);
                float3 diffuseAl = kDal * li.albedo.xyz * orenNayarAl;

                float3 alColor = UnpackUnorm4x8(vs.packedColor).rgb;
                float3 commonTerm = s.cosShade * LightGeometryTerm(s) * vs.intensity * alColor * s.attenuation * r.W;

                diffuseRadiance += diffuseAl * commonTerm;
                float3 specContribution = specularAl * commonTerm;
                specularRadiance += specContribution;

                float specLum = dot(specContribution, float3(0.2126, 0.7152, 0.0722));
                if (specLum > bestSpecLum)
                {
                    bestSpecLum = specLum;
                    hitDistance = s.dist;
                }
            }
        }
    }

    float3 diffuseOut = diffuseRadiance / max(li.albedo.rgb, float3(0.01, 0.01, 0.01));

    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
    float3 specReflectance = F0 * envBRDF.x + envBRDF.y;
    float3 specularOut = specularRadiance / max(specReflectance, float3(0.01, 0.01, 0.01));

    float hitT = (isnan(hitDistance) || isinf(hitDistance)) ? 0.0 : hitDistance;
    float4 diffPacked = float4(SanitizeRadiance(diffuseOut), hitT);
    float4 specPacked = float4(SanitizeRadiance(specularOut), hitT);

    uint2 writeCoord = (field != 0u && pc.bCheckerboardPacked != 0u) ? rcoord : pixel;
    rdgStorageFloat4[pc.primaryOutputImageIndex][writeCoord] = diffPacked;
    rdgStorageFloat4[pc.secondaryOutputImageIndex][writeCoord] = specPacked;

    if (field != 0u && pc.bCheckerboardPacked == 0u) {
        int2 o = OtherFieldPixel(pixel, field);
        if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
            rdgStorageFloat4[pc.primaryOutputImageIndex][uint2(o)] = float4(diffPacked.rgb, 0.0);
            rdgStorageFloat4[pc.secondaryOutputImageIndex][uint2(o)] = float4(specPacked.rgb, 0.0);
        }
    }
}
