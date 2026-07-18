/*
 * SIGMA shadow denoiser: temporal stabilization (mirrors NRD SIGMA_TemporalStabilization).
 * Reprojects the previous stabilized shadow by motion vector, rejects it on disocclusion via a
 * plane-distance test against the per-pixel (viewZ, accumFrames) packed into the history-length texture,
 * clamps it to the local 5x5 mean +/- variance (sigma scaled by history length), and blends with an
 * antilag-modulated, history-length-driven weight. All math in linear shadow; storage is sqrt-encoded.
 */
import common_interop;
import push_constant_interop;
import constants_interop;
import bindless_declarations;
import packing_functions;
import shader_functions;
import denoiser_common;
import sigma_common;

// viewZ float bit pattern with its low 3 mantissa bits replaced by the accum frame count (0-7)
static uint PackViewZAndHistoryLength(float viewZ, float historyLength)
{
    uint p = asuint(viewZ) & ~7u;
    p |= min(uint(historyLength + 0.5), 7u);
    return p;
}

static float UnpackHistoryViewZ(uint p) { return asfloat(p & ~7u); }
static float UnpackHistoryLength(uint p) { return float(p & 7u); }

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeSigmaShadowTemporal(uniform SigmaTemporalPushConstant pc, uint3 tid : SV_DispatchThreadID)
{
    uint2 pixel = tid.xy;
    if (any(pixel >= pc.renderExtent)) {
        return;
    }

    SceneData scene = pc.sceneData[pc.sceneDataIndex];
    float rawDepth = rdgTexturesFloat[pc.depthIndex].Load(int3(pixel, 0)).r;

    float2 cur = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pixel, 0)).rg;
    float centerPenumbra = cur.g;

    if (rawDepth <= 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = cur;
        rdgStorageUInt[pc.outHistoryLengthIndex][pixel] = PackViewZAndHistoryLength(0.0, 0.0);
        return;
    }

    float centerViewZ = ScreenToViewSpaceDepth(rawDepth, scene.depthLinearizeMult, scene.depthLinearizeAdd);
    float2 pixelUv = (float2(pixel) + 0.5) / float2(pc.renderExtent);
    float tileValue = SigmaTextureCubic(pc.tilesIndex, pixelUv).y;

    // Hard shadow / uniform tile: nothing to stabilize. Keep MAX history length so neighbors may still accumulate.
    if (tileValue == 0.0 || centerPenumbra == 0.0) {
        rdgStorageFloat2[pc.outputIndex][pixel] = cur;
        rdgStorageUInt[pc.outHistoryLengthIndex][pixel] = PackViewZAndHistoryLength(centerViewZ, SIGMA_MAX_ACCUM_FRAME_NUM);
        return;
    }

    // Local mean / variance over 5x5 (linear shadow space).
    float sumW = 0.0;
    float m1 = 0.0;
    float m2 = 0.0;
    float input = SigmaUnpackShadow(cur.r);

    [unroll]
    for (int j = 0; j <= SIGMA_BORDER * 2; j++)
    {
        [unroll]
        for (int i = 0; i <= SIGMA_BORDER * 2; i++)
        {
            int2 d = int2(i - SIGMA_BORDER, j - SIGMA_BORDER);
            int2 pos = clamp(int2(pixel) + d, int2(0), int2(pc.renderExtent) - 1);
            float2 sd = rdgTexturesFloat2[pc.shadowIndex].Load(int3(pos, 0)).rg;
            float s = SigmaUnpackShadow(sd.r);

            float w = 1.0;
            if (!(i == SIGMA_BORDER && j == SIGMA_BORDER)) {
                w = SigmaAreBothLitOrUnlit(centerPenumbra, sd.g);
                w *= RelaxGetGaussianWeight(length(float2(d) / float(SIGMA_BORDER)));
            }

            m1 += s * w;
            m2 += s * s * w;
            sumW += w;
        }
    }

    m1 /= sumW;
    m2 /= sumW;
    float sigma = sqrt(max(0.0, m2 - m1 * m1));

    // Reproject previous stabilized shadow.
    uint4 gt1 = rdgTexturesUInt4[pc.gbufferOneIndex].Load(int3(pixel, 0));
    float2 surfUv = pixelUv;
    if (pc.pixelScale > 1u) {
        // Half res: rawDepth belongs to the selected full-res corner, so reconstruct at the corner's true UV, not the half-res texel center.
        uint quadSel = gt1.a & 3u;
        uint2 fullPixel = pixel * pc.pixelScale + uint2(quadSel & 1u, (quadSel >> 1u) & 1u);
        surfUv = (float2(fullPixel) + 0.5) / scene.mainRenderTargetSize;
    }
    float3 worldPos = ReconstructWorldPosition(scene.invViewProj, surfUv, rawDepth);
    float2 velocity = UnpackMotionVectors(gt1.g) * 0.5;
    float2 smbPixelUv = pixelUv + velocity;

    float historyLength = 0.0;
    float history = input;

    bool hasHistory = pc.historyIndex != ~0u && pc.historyLengthIndex != ~0u;
    if (hasHistory && all(smbPixelUv > float2(0.0)) && all(smbPixelUv < float2(1.0)))
    {
        float expectedPrevViewZ = -(mul(scene.prevView, float4(worldPos, 1.0)).z);
        float frustumSize = SigmaFrustumSize(scene.ndcToViewMul, centerViewZ, pc.renderExtent);
        float3 Xv = CheapReconstructViewSpacePosition(scene.ndcToViewMul, scene.ndcToViewAdd, surfUv, centerViewZ);
        float3 Nv = normalize(UnpackNormalOct16(gt1.r));
        float NoV = abs(dot(Nv, normalize(-Xv)));

        MLBilinear f = MLGetBilinearFilter(smbPixelUv, float2(pc.renderExtent));
        int2 maxCoord = int2(pc.renderExtent) - 1;
        int2 o00 = clamp(int2(f.origin) + int2(0, 0), int2(0), maxCoord);
        int2 o10 = clamp(int2(f.origin) + int2(1, 0), int2(0), maxCoord);
        int2 o01 = clamp(int2(f.origin) + int2(0, 1), int2(0), maxCoord);
        int2 o11 = clamp(int2(f.origin) + int2(1, 1), int2(0), maxCoord);

        uint p00 = rdgTexturesUInt[pc.historyLengthIndex].Load(int3(o00, 0));
        uint p10 = rdgTexturesUInt[pc.historyLengthIndex].Load(int3(o10, 0));
        uint p01 = rdgTexturesUInt[pc.historyLengthIndex].Load(int3(o01, 0));
        uint p11 = rdgTexturesUInt[pc.historyLengthIndex].Load(int3(o11, 0));

        float4 prevViewZ = float4(UnpackHistoryViewZ(p00), UnpackHistoryViewZ(p10), UnpackHistoryViewZ(p01), UnpackHistoryViewZ(p11));
        float4 prevAccum = float4(UnpackHistoryLength(p00), UnpackHistoryLength(p10), UnpackHistoryLength(p01), UnpackHistoryLength(p11));

        float4 inScreen = RelaxIsInScreenBilinear(f.origin, float2(pc.renderExtent));
        float4 threshold = SigmaDisocclusionThreshold(frustumSize, NoV) * inScreen - NRD_EPS;
        float4 planeDist = abs(prevViewZ - expectedPrevViewZ);
        float4 occlusion = step(planeDist, threshold);

        float4 weights = MLGetBilinearCustomWeights(f, occlusion);

        historyLength = RelaxBilinearWeightedFloat(prevAccum.x, prevAccum.y, prevAccum.z, prevAccum.w, weights);

        bool isCatRomAllowed = dot(weights, 1.0) > 3.5;
        float historySqrt = SigmaBicubicHistory(pc.historyIndex, saturate(smbPixelUv) * float2(pc.renderExtent), 1.0 / float2(pc.renderExtent), weights, isCatRomAllowed);
        history = SigmaUnpackShadow(saturate(historySqrt));
    }

    // Variance clamp (tighter when history is long).
    sigma *= lerp(SIGMA_TS_SIGMA_SCALE, 1.0, 1.0 / (1.0 + historyLength));
    float historyClamped = clamp(history, m1 - sigma, m1 + sigma);

    // Antilag from the amount of clamping, folded back into history length.
    float antilag = saturate(1.0 - sqrt(saturate(abs(historyClamped - history))));
    historyLength *= antilag;

    float historyWeight = historyLength / (1.0 + historyLength);

    // "Street magic": smooth out penumbra-to-1 regions.
    float streetMagic = 0.6 * historyWeight * antilag;
    historyClamped = lerp(historyClamped, history, streetMagic);

    float result = lerp(input, historyClamped, min(pc.stabilizationStrength, historyWeight));

    historyLength = min(historyLength + 1.0, SIGMA_MAX_ACCUM_FRAME_NUM);

    rdgStorageFloat2[pc.outputIndex][pixel] = float2(SigmaPackShadow(result), centerPenumbra);
    rdgStorageUInt[pc.outHistoryLengthIndex][pixel] = PackViewZAndHistoryLength(centerViewZ, historyLength);
}
