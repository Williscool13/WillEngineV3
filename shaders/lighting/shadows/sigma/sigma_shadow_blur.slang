/*
 * SIGMA shadow denoiser: penumbra-aware spatial filter (1:1 NRD SIGMA_Blur, FIRST_PASS via passIndex).
 * The input is a stochastic 1spp binary shadow (1 lit / 0 occluded) plus a per-pixel penumbra radius; the
 * soft gradient is reconstructed here by blurring the binary shadow with a kernel sized to the local penumbra.
 *   pass 0 (blur):      derive shadow from the trace penumbra (IsLit), estimate penumbra, filter.
 *   pass 1 (post-blur): unpack the sqrt-encoded shadow from pass 0, re-estimate penumbra, filter again.
 * Dense 5x5 keeps small penumbrae crisp (center-tap fallback); sparse 8-tap (g_Special8) covers wide
 * penumbrae with a screen-space anisotropic kernel. All math is in linear shadow; storage is sqrt-encoded.
 * Output R16G16: .r = sqrt(visibility), .g = estimated penumbra (world).
 */
import common_interop;
import push_constant_interop;
import lights_interop;
import constants_interop;
import bindless_declarations;
import packing_functions;
import shader_functions;
import denoiser_common;
import sigma_common;

/** Shadow value at a pixel: pass 0 derives it from the penumbra sentinel, pass 1 unpacks sqrt storage. */
static float LoadShadow(uint passIndex, uint shadowIndex, int2 pos)
{
    float2 d = rdgTexturesFloat2[shadowIndex].Load(int3(pos, 0)).rg;
    if (passIndex == 0u) {
        return SigmaIsLit(d.g) ? 1.0 : 0.0;
    }
    return SigmaUnpackShadow(d.r);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeSigmaShadowBlur(uniform SigmaBlurPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 pixel = tid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float rawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;
    float centerPenumbra = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).g;

    if (rawDepth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(1.0, centerPenumbra);
        return;
    }

    float2 pixelUv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float tileValue = SigmaTextureCubic(pc.tilesIndex, pixelUv).y;

    float centerShadow = LoadShadow(pc.passIndex, pc.shadowIndex, int2(pixel));

    // Uniform tile or hard contact: nothing to soften, pass the binary shadow straight through.
    if (tileValue == 0.0 || centerPenumbra == 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = float2(SigmaPackShadow(centerShadow), centerPenumbra);
        return;
    }

    float centerViewZ = ScreenToViewSpaceDepth(rawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float3 Xv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, pixelUv, centerViewZ);
    float3 Nv = normalize(UnpackNormalOct16(rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0)).r));

    float pixelSize = SigmaPixelSize(scene.ndcToViewMul, centerViewZ, pc.renderExtent.y);
    float frustumSize = SigmaFrustumSize(scene.ndcToViewMul, centerViewZ, pc.renderExtent);
    float3 Vv = normalize(-Xv);
    float NoV = abs(dot(Nv, Vv));
    float2 geometryWeightParams = RelaxGetGeometryWeightParams(SIGMA_PLANE_DIST_SENSITIVITY, frustumSize, Xv, Nv);

    // Dense 5x5: filter the shadow and estimate the local penumbra (weighted toward smaller penumbra).
    float2 sum = float2(0.0, 0.0);
    float penumbra = 0.0;
    float result = 0.0;
    float centerTap = centerShadow;

    [unroll]
    for (int j = 0; j <= SIGMA_BORDER * 2; j++)
    {
        [unroll]
        for (int i = 0; i <= SIGMA_BORDER * 2; i++)
        {
            int2 d = int2(i - SIGMA_BORDER, j - SIGMA_BORDER);
            int2 pos = int2(pixel) + d;
            bool inside = all(pos >= int2(0)) && all(pos < int2(pc.renderExtent));
            pos = clamp(pos, int2(0), int2(pc.renderExtent) - 1);

            float penum = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pos, 0)).g;
            float sRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pos, 0)).r;
            float s = LoadShadow(pc.passIndex, pc.shadowIndex, pos);

            float w = 1.0;
            if (i == SIGMA_BORDER && j == SIGMA_BORDER) {
                centerTap = s;
            } else {
                float2 uv = pixelUv + float2(d) / float2(pc.renderExtent);
                float zs = ScreenToViewSpaceDepth(sRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
                float3 Xvs = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, zs);
                float NoX = dot(Nv, Xvs);

                w *= SigmaAreBothLitOrUnlit(centerPenumbra, penum);
                w *= RelaxGetGaussianWeight(length(float2(d) / float(SIGMA_BORDER)));
                w = RelaxApplyGeometryWeightLast(w, zs, NoX, geometryWeightParams, NRD_INF);
                w *= (inside && sRawDepth > 0.0) ? 1.0 : 0.0;
            }

            result += w == 0.0 ? 0.0 : s * w;
            sum.x += w;

            float wp = w * (pixelSize / (pixelSize + penum)) * (SigmaIsLit(penum) ? 0.0 : 1.0);
            penumbra += wp == 0.0 ? 0.0 : penum * wp;
            sum.y += wp;
        }
    }

    result /= sum.x;
    sum.x = 1.0;
    penumbra /= max(sum.y, NRD_EPS);
    sum.y = float(sum.y != 0.0);

    // Avoid blurry result if penumbra size < NRD_BORDER: blend back to the unfiltered center tap.
    float penumbraInPixels = penumbra / pixelSize;
    float f = MLSmoothStep(0.0, float(SIGMA_BORDER), penumbraInPixels);
    result = lerp(centerTap, result, f);

    // Avoid unnecessary weight increase for the unfiltered center sample if the blur radius is small.
    f = lerp(4.0, 1.0, f);
    result *= f;
    penumbra *= f;
    sum *= f;

    float blurRadius = SigmaGetKernelRadiusInPixels(penumbra * pc.penumbraScale, pixelSize, pc.maxKernelPixels, tileValue);

    // Per-frame rotator (NRD_FRAME) + screen-space anisotropic skew (NRD SIGMA_USE_SCREEN_SPACE_SAMPLING).
    float4 rotator = SigmaBlurRotator(pc.frameIndex, pc.passIndex);
    float2 skew = lerp(1.0 - abs(Nv.xy), float2(1.0), NoV);
    skew /= max(skew.x, skew.y);
    float2 skewScaled = skew * blurRadius / float2(pc.renderExtent);

    float invEstimatedPenumbra = 1.0 / max(penumbra, NRD_EPS);

    [unroll]
    for (uint n = 0; n < 8; n++)
    {
        float3 offset = SIGMA_SPECIAL8[n];
        float2 uv = pixelUv + skewScaled * MLRotateVector(rotator, offset.xy);
        // Snap to the pixel center.
        uv = (floor(uv * float2(pc.renderExtent)) + 0.5) / float2(pc.renderExtent);

        float inScreen = RelaxIsInScreenNearest(uv);
        if (inScreen == 0.0) { continue; }
        int2 pos = clamp(int2(floor(uv * float2(pc.renderExtent))), int2(0), int2(pc.renderExtent) - 1);

        float penum = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pos, 0)).g;
        float sRawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pos, 0)).r;
        if (sRawDepth <= 0.0) { continue; }
        float zs = ScreenToViewSpaceDepth(sRawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
        float3 Xvs = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, uv, zs);
        float NoX = dot(Nv, Xvs);
        float s = LoadShadow(pc.passIndex, pc.shadowIndex, pos);

        float w = inScreen;
        w *= SigmaAreBothLitOrUnlit(centerPenumbra, penum);
        w *= RelaxGetGaussianWeight(offset.z);
        w *= saturate(penum * invEstimatedPenumbra);
        w = RelaxApplyGeometryWeightLast(w, zs, NoX, geometryWeightParams, NRD_INF);

        result += w == 0.0 ? 0.0 : s * w;
        sum.x += w;

        float wp = w * (pixelSize / (pixelSize + penum)) * (SigmaIsLit(penum) ? 0.0 : 1.0);
        penumbra += wp == 0.0 ? 0.0 : penum * wp;
        sum.y += wp;
    }

    result /= sum.x;
    penumbra = sum.y == 0.0 ? centerPenumbra : penumbra / sum.y;

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(SigmaPackShadow(result), penumbra);
}
