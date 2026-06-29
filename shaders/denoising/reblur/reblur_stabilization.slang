/*
 * ReBLUR: TemporalStabilization. Faithful port of NRD REBLUR_TemporalStabilization (DIFFUSE_SPECULAR, YCoCg=1).
 * Reprojects the carried stabilized history by surface motion, builds a per-signal luma neighborhood box
 * (mean + sigma) over the current post-blur signal, computes the ReBLUR antilag factor, firefly-clamps the
 * current frame, clamps the reprojected history luma to the box, then blends current -> history in YCoCg to
 * maximize temporal stability. Writes the stabilized history (carried) and the final linear-RGB result into
 * the engine intermediates. Pass-through on sky / out-of-range / when stabilization is disabled.
 * Adapted from NVIDIA Real-Time Denoisers (NRD). Copyright (c) 2022-2024 NVIDIA Corporation. All rights reserved.
 * https://github.com/NVIDIA-RTX/NRD -- NVIDIA RTX SDKs LICENSE
 */

import push_constant_interop;
import bindless_declarations;
import constants_interop;
import reblur_interop;
import relax_utils;
import reblur_common;
import shader_functions;

/** 3x3 luma mean + standard deviation over a YCoCg signal (luma = .x). NRD builds these moments from the
 *  current post-blur signal and uses them for antilag, the firefly clamp and the history color box. */
void NeighborhoodLumaMoments(uint texIdx, int2 pixelPos, int2 rectSize, out float mean, out float sigma)
{
    float m1 = 0.0;
    float m2 = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            int2 p = clamp(pixelPos + int2(x, y), int2(0, 0), rectSize - 1);
            float luma = ReblurGetLuma(rdgTexturesFloat4[texIdx].Load(int3(p, 0)));
            m1 += luma;
            m2 += luma * luma;
        }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    mean = m1;
    sigma = sqrt(max(m2 - m1 * m1, 0.0));
}

/** Stabilize one signal (YCoCg + normalized hit distance). 'current' is the post-blur frame, 'prevStab' the
 *  reprojected stabilized history. Mirrors the NRD luma path: antilag -> history weight -> firefly clamp of
 *  current -> color-box clamp of history -> blend. */
float4 Stabilize(float4 current, float4 prevStab, bool historyValid, float mean, float sigma, float accumFrames, ReblurDiffuseSpecularConstants* rc)
{
    // Antilag from luma deviation (NRD ComputeAntilag, ANTILAG_MODE 2): uses both gAntilagSettings.x (sigma
    // scale) and .y (sensitivity). accumSpeed = accumulated frame count.
    float historyLuma = ReblurGetLuma(prevStab);
    float antilag = ReblurComputeAntilag(historyLuma, mean, sigma, accumFrames, rc.gAntilagSettings, rc.gFramerateScale);

    // History weight: convergence curve * stabilization strength * antilag. Disabled => caller passes weight 0.
    float stabFrames = min(rc.gMaxStabilizedFrameNum, accumFrames);
    float convergence = stabFrames / (1.0 + stabFrames);
    float historyWeight = historyValid ? convergence * rc.gStabilizationStrength * antilag : 0.0;

    // Color-box sigma scale (NRD GetTemporalAccumulationParams.y form: 1 + 3 * framerateScale * w).
    float clampScale = 1.0 + 3.0 * rc.gFramerateScale * (convergence * antilag);

    // Firefly clamp the current frame so spikes never get baked into the stabilized history.
    float currentLuma = ReblurGetLuma(current);
    float4 currentClamped = ReblurChangeLuma(current, min(currentLuma, mean + sigma * clampScale));

    // Clamp the reprojected history luma to the neighborhood box (NRD Color::Clamp).
    float clampedHistoryLuma = clamp(historyLuma, mean - sigma * clampScale, mean + sigma * clampScale);
    float4 historyClamped = ReblurChangeLuma(prevStab, clampedHistoryLuma);

    // Blend current -> clamped history in YCoCg (hit distance in .w follows the same lerp).
    return lerp(currentClamped, historyClamped, historyWeight);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReblurStabilizationMain(uniform ReblurStabilizationPushConstant pc, int2 pixelPos : SV_DispatchThreadID)
{
    ReblurDiffuseSpecularConstants* rc = pc.constants;

    float4 specCur = rdgTexturesFloat4[pc.specHistIndex].Load(int3(pixelPos, 0));
    float4 diffCur = rdgTexturesFloat4[pc.diffHistIndex].Load(int3(pixelPos, 0));

    float isSky = rdgTexturesFloat[pc.tilesIndex].Load(int3(pixelPos >> 4, 0)).r;
    bool outOfRange = isSky != 0.0 || pixelPos.x >= rc.gRectSize.x || pixelPos.y >= rc.gRectSize.y;
    if (!outOfRange) {
        float viewZ = RelaxLoadViewZ(pc.viewZIndex, pixelPos, rc.depthLinearizeMult, rc.depthLinearizeAdd);
        outOfRange = !RelaxIsInDenoisingRange(viewZ, rc.gDenoisingRange);
    }

    float4 specStab = specCur;
    float4 diffStab = diffCur;

    if (!outOfRange && rc.gMaxStabilizedFrameNum > 0.5) {
        // Surface-motion reprojection (gbuffer MV in .g). Full-res: index by pixelPos directly.
        float2 pixelUv = (float2(pixelPos) + 0.5) * rc.gRectSizeInv;
        uint4 gt1 = rdgTexturesUInt4[pc.normalRoughnessIndex].Load(int3(pixelPos, 0));
        float2 prevUv = pixelUv + UnpackMotionVectors(gt1.g) * rc.gMvScale.xy;

        bool historyValid = RelaxIsInScreenNearest(prevUv) == 1.0 && rc.gResetHistory == 0;

        float4 prevSpec = specCur;
        float4 prevDiff = diffCur;
        if (historyValid) {
            SamplerState lin = rdgSamplers[RDG_LINEAR_SAMPLER_INDEX];
            float2 prevUvRes = prevUv * rc.gRectSizePrev * rc.gResourceSizeInvPrev;
            prevSpec = ReblurClampNegativeToZero(rdgTexturesFloat4[pc.prevSpecStabIndex].SampleLevel(lin, prevUvRes, 0));
            prevDiff = ReblurClampNegativeToZero(rdgTexturesFloat4[pc.prevDiffStabIndex].SampleLevel(lin, prevUvRes, 0));
        }

        // Accumulated frame count (engine DATA1 substitute: RELAX-style history length, stored as frames/255).
        float accumFrames = RELAX_MAX_ACCUM_FRAME_NUM * rdgTexturesFloat[pc.historyLengthIndex].Load(int3(pixelPos, 0)).r;

        float specMean, specSigma, diffMean, diffSigma;
        NeighborhoodLumaMoments(pc.specHistIndex, pixelPos, rc.gRectSize, specMean, specSigma);
        NeighborhoodLumaMoments(pc.diffHistIndex, pixelPos, rc.gRectSize, diffMean, diffSigma);

        specStab = Stabilize(specCur, prevSpec, historyValid, specMean, specSigma, accumFrames, rc);
        diffStab = Stabilize(diffCur, prevDiff, historyValid, diffMean, diffSigma, accumFrames, rc);
    }

    // Carried stabilized history (YCoCg + normalized hit distance) for next-frame reprojection.
    rdgStorageFloat4[pc.outSpecStabIndex][pixelPos] = specStab;
    rdgStorageFloat4[pc.outDiffStabIndex][pixelPos] = diffStab;

    // Final linear-RGB result for remodulation (max 0); keep normalized hit distance in .w.
    rdgStorageFloat4[pc.outSpecFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(specStab.xyz), 0.0), specStab.w);
    rdgStorageFloat4[pc.outDiffFinalIndex][pixelPos] = float4(max(MLYCoCgToRgb(diffStab.xyz), 0.0), diffStab.w);
}
