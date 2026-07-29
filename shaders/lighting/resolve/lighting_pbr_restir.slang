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

// Directional-light hit distance for the denoiser's specular hitT channel; matches the sun trace TMax.
static const float SUN_SPEC_HIT_T = 10000.0;
static const float NEIGHBOR_REJECT_DISTANCE = 3.0e+38;

float3 SanitizeRadiance(float3 c) {
    c.x = (isnan(c.x) || isinf(c.x)) ? 0.0 : c.x;
    c.y = (isnan(c.y) || isinf(c.y)) ? 0.0 : c.y;
    c.z = (isnan(c.z) || isinf(c.z)) ? 0.0 : c.z;
    return clamp(c, 0.0, 65504.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void ComputeLightingPBRRestir(uniform VisibilityLightingPushConstant pc, uint3 threadID : SV_DispatchThreadID) {
    LightingDispatchParameters lightParams = pc.lightDispatchBuffer[pc.lightingIndex];
    uint2 pixel = threadID.xy + uint2(lightParams.minX, lightParams.minY);
    if (any(pixel >= uint2(lightParams.maxX, lightParams.maxY))) {
        return;
    }

    uint field = pc.activeCheckerboardField;
    bool bFullRate = pc.bFullRateResolve != 0u;
    bool bHole = field != 0u && !IsActiveCheckerboardPixel(int2(pixel), false, field);
    if (bHole && !bFullRate) {
        return;
    }
    uint2 rcoord = PixelPosToReservoirPos(pixel, field);

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    uint2 fullPixel = pixel;

    float depth = rdgTexturesFloat[pc.depthIndex].Load(int3(fullPixel, 0)).r;
    if (depth <= 0.0) {
        return;
    }

    LightingInputs li = ReconstructLightingInputs(fullPixel, pc.visibilityBufferIndex, pc.lightingIndex, pc.instanceBuffer, pc.gbufferOneIndex, pc.gbufferTwoIndex, ~0u);
    if (!li.valid) {
        return;
    }

    float2 uv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float viewSpaceDepth = ScreenToViewSpaceDepth(depth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 viewPosition = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, viewSpaceDepth);

    if (bHole) {
        int xl = int(pixel.x) - 1;
        int xr = int(pixel.x) + 1;
        float dl = NEIGHBOR_REJECT_DISTANCE;
        float dr = NEIGHBOR_REJECT_DISTANCE;
        if (xl >= 0) {
            float rawL = rdgTexturesFloat[pc.depthIndex].Load(int3(xl, int(pixel.y), 0)).r;
            if (rawL > 0.0) { dl = abs(ScreenToViewSpaceDepth(rawL, scene.depthLinearizeMult, scene.depthLinearizeAdd) - viewSpaceDepth); }
        }
        if (xr < int(pc.renderExtent.x)) {
            float rawR = rdgTexturesFloat[pc.depthIndex].Load(int3(xr, int(pixel.y), 0)).r;
            if (rawR > 0.0) { dr = abs(ScreenToViewSpaceDepth(rawR, scene.depthLinearizeMult, scene.depthLinearizeAdd) - viewSpaceDepth); }
        }
        int borrowX = (dl <= dr) ? max(xl, 0) : min(xr, int(pc.renderExtent.x) - 1);
        rcoord = uint2(uint(borrowX) >> 1u, pixel.y);
    }

    float3 N = normalize(li.viewNormal);
    float3 V = normalize(-viewPosition);

    float roughness = max(li.roughness, RESTIR_MIN_ROUGHNESS);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), li.albedo.xyz, li.metallic);
    float nDotV = max(dot(N, V), 0.0);

    float3 diffuseRadiance = float3(0.0);
    float3 specularRadiance = float3(0.0);
    float diffHitDistance = 0.0;
    float specHitDistance = 0.0;
    float bestDiffLum = -1.0;
    float bestSpecLum = -1.0;

    uint stride = RestirCheckerboardStride(pc.renderExtent.x, field);
    uint pixelIdx = rcoord.y * stride + rcoord.x;
    uint sunIdx = bFullRate ? (pixel.y * pc.renderExtent.x + pixel.x) : pixelIdx;

    bool bAnyLight = pc.reservoirBuffer != 0;
    if (bAnyLight)
    {
        Reservoir r = pc.reservoirBuffer[pixelIdx];

        if (r.lightIdx != ~0u && r.W > 0.0 && r.lightIdx < uint(pc.lightData->lightCount))
        {
            LightView vs = UnpackLightVS(pc.lightVS[r.lightIdx]);
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

                float3 diffContribution = diffuseAl * commonTerm;
                diffuseRadiance += diffContribution;

                float diffLum = dot(diffContribution, float3(0.2126, 0.7152, 0.0722));
                if (diffLum > bestDiffLum)
                {
                    bestDiffLum = diffLum;
                    diffHitDistance = s.dist;
                }

                // Defer specular from local lights to the traced reflection pass across its whole eligible roughness range
                if (roughness > pc.lightSpecularFromReflectionsMax)
                {
                    float3 specContribution = specularAl * commonTerm;
                    specularRadiance += specContribution;

                    float specLum = dot(specContribution, float3(0.2126, 0.7152, 0.0722));
                    if (specLum > bestSpecLum)
                    {
                        bestSpecLum = specLum;
                        specHitDistance = s.dist;
                    }
                }
            }
        }
    }

    if (pc.sunReservoirBuffer != 0)
    {
        Reservoir rs = pc.sunReservoirBuffer[sunIdx];
        // lightIdx: ~0u empty, 0u winner shadowed, 1u winner lit; W excludes visibility (see restir_di_sun.slang).
        if (rs.lightIdx == 1u && rs.W > 0.0)
        {
            DirectionalLightData sun = pc.lightData->directionalLight;
            float2 sunXi = UnpackUnorm2x16(rs.sampleOffsetPacked);
            float sunPdf;
            float3 sunDirW = SampleSunConeDirection(SunDirectionWorld(sun), sun.angularRadius, sunXi.x, sunXi.y, sunPdf);
            float3 Lsun = normalize(mul(float3x3(scene.view), sunDirW));
            float nDotLsun = dot(N, Lsun);
            if (nDotLsun > 0.0)
            {
                float3 Hsun = normalize(V + Lsun);
                float NDFsun = D_GGX(N, Hsun, roughness);
                float Gsun = G_SMITH_HEIGHT_CORRELATED(N, V, Lsun, roughness);
                float3 Fsun = F_SCHLICK(V, Hsun, F0);
                float3 specularSun = (NDFsun * Gsun * Fsun) / max(4.0 * nDotV * nDotLsun, 0.001);

                float3 kDsun = (1.0 - Fsun) * (1.0 - li.metallic);
                float orenNayarSun = Diffuse_OrenNayar(nDotLsun, nDotV, Lsun, V, roughness);

                float3 commonSun = SunRadiance(sun) * nDotLsun * rs.W;
                float3 diffContribution = kDsun * li.albedo.xyz * orenNayarSun * commonSun;
                diffuseRadiance += diffContribution;

                float diffLum = dot(diffContribution, float3(0.2126, 0.7152, 0.0722));
                if (diffLum > bestDiffLum)
                {
                    bestDiffLum = diffLum;
                    diffHitDistance = SUN_SPEC_HIT_T;
                }

                // Defer specular from local lights to the traced reflection pass across its whole eligible roughness range
                if (roughness > pc.lightSpecularFromReflectionsMax)
                {
                    float3 specContribution = specularSun * commonSun;
                    specularRadiance += specContribution;

                    float specLum = dot(specContribution, float3(0.2126, 0.7152, 0.0722));
                    if (specLum > bestSpecLum)
                    {
                        bestSpecLum = specLum;
                        specHitDistance = SUN_SPEC_HIT_T;
                    }
                }
            }
        }
    }

    float3 diffuseOut = diffuseRadiance / max(li.albedo.rgb, float3(0.01, 0.01, 0.01));

    float2 envBRDF = assetTextures[BRDF_LUT_BINDLESS_INDEX].SampleLevel(assetSamplers[ASSET_SAMPLER_LINEAR_BINDLESS_INDEX], float2(nDotV, roughness), 0).rg;
    float3 specReflectance = F0 * envBRDF.x + envBRDF.y;
    float3 specularOut = specularRadiance / max(specReflectance, float3(0.01, 0.01, 0.01));

    float diffHitT = (isnan(diffHitDistance) || isinf(diffHitDistance)) ? 0.0 : diffHitDistance;
    float specHitT = (isnan(specHitDistance) || isinf(specHitDistance)) ? 0.0 : specHitDistance;
    float4 diffPacked = float4(SanitizeRadiance(diffuseOut), diffHitT);
    float4 specPacked = float4(SanitizeRadiance(specularOut), specHitT);

    uint2 writeCoord = (field != 0u && !bFullRate && pc.bCheckerboardPacked != 0u) ? PixelPosToReservoirPos(pixel, field) : pixel;
    rdgStorageFloat4[pc.primaryOutputImageIndex][writeCoord] = diffPacked;
    rdgStorageFloat4[pc.secondaryOutputImageIndex][writeCoord] = specPacked;

    if (field != 0u && !bFullRate && pc.bCheckerboardPacked == 0u) {
        int2 o = OppositeFieldPixel(pixel, field);
        if (o.x >= 0 && o.x < int(pc.renderExtent.x)) {
            // Keep hitT in the duplicated field: a zeroed .w oscillates the accumulated hit distance toward
            // half its value and trips the miss paths in the ReBLUR prepass.
            rdgStorageFloat4[pc.primaryOutputImageIndex][uint2(o)] = diffPacked;
            rdgStorageFloat4[pc.secondaryOutputImageIndex][uint2(o)] = specPacked;
        }
        // Odd-width orphan: on phantom rows the inactive rightmost column has no active partner writing into it; the last active column (x-2) fills it here.
        if ((pc.renderExtent.x & 1u) != 0u && pixel.x == pc.renderExtent.x - 2u && ((pixel.y + field) & 1u) == 1u) {
            uint2 orphan = uint2(pc.renderExtent.x - 1u, pixel.y);
            rdgStorageFloat4[pc.primaryOutputImageIndex][orphan] = diffPacked;
            rdgStorageFloat4[pc.secondaryOutputImageIndex][orphan] = specPacked;
        }
    }
}
