//
// Created by William on 2026-06-13.
//

#include "config_serialization.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "render/interface/render_interface.h"
#include "render/shaders/reflection_interop.h"

namespace Engine::ConfigSerialization
{
static void SerializeRelax(const Core::RELAXParams& rx, TextWriter& w)
{
    w.Key("denoisingRange", rx.denoisingRange);
    w.Key("disocclusionThreshold", rx.disocclusionThreshold);
    w.Key("depthThreshold", rx.depthThreshold);
    w.Key("specMaxAccumFrames", rx.specMaxAccumFrames);
    w.Key("specMaxFastAccumFrames", rx.specMaxFastAccumFrames);
    w.Key("diffMaxAccumFrames", rx.diffMaxAccumFrames);
    w.Key("diffMaxFastAccumFrames", rx.diffMaxFastAccumFrames);
    w.Key("historyAccelerationAmount", rx.historyAccelerationAmount);
    w.Key("diffBlurRadius", rx.diffBlurRadius);
    w.Key("specBlurRadius", rx.specBlurRadius);
    w.Key("minHitDistanceWeight", rx.minHitDistanceWeight);
    w.Key("atrousIterations", rx.atrousIterations);
    w.Key("lobeAngleFraction", rx.lobeAngleFraction);
    w.Key("roughnessFraction", rx.roughnessFraction);
    w.Key("specLobeAngleSlack", rx.specLobeAngleSlack);
    w.Key("specPhiLuminance", rx.specPhiLuminance);
    w.Key("diffPhiLuminance", rx.diffPhiLuminance);
    w.Key("diffMaxLuminanceRelativeDifference", rx.diffMaxLuminanceRelativeDifference);
    w.Key("specMaxLuminanceRelativeDifference", rx.specMaxLuminanceRelativeDifference);
    w.Key("luminanceEdgeStoppingRelaxation", rx.luminanceEdgeStoppingRelaxation);
    w.Key("normalEdgeStoppingRelaxation", rx.normalEdgeStoppingRelaxation);
    w.Key("roughnessEdgeStoppingRelaxation", rx.roughnessEdgeStoppingRelaxation);
    w.Key("specVarianceBoost", rx.specVarianceBoost);
    w.Key("roughnessEdgeStoppingEnabled", rx.roughnessEdgeStoppingEnabled);
    w.Key("historyFixEdgeStoppingNormalPower", rx.historyFixEdgeStoppingNormalPower);
    w.Key("historyFixFrameNum", rx.historyFixFrameNum);
    w.Key("historyFixBasePixelStride", rx.historyFixBasePixelStride);
    w.Key("fastHistoryClampingSigmaScale", rx.fastHistoryClampingSigmaScale);
    w.Key("historyResetTemporalSigmaScale", rx.historyResetTemporalSigmaScale);
    w.Key("historyResetSpatialSigmaScale", rx.historyResetSpatialSigmaScale);
    w.Key("historyResetAmount", rx.historyResetAmount);
    w.Key("enablePrepass", rx.enablePrepass);
    w.Key("enableAntiFirefly", rx.enableAntiFirefly);
    w.Key("bChromaAtrous", rx.bChromaAtrous);
    w.Key("chromaAtrousIterations", rx.chromaAtrousIterations);
    w.Key("chromaLumaPower", rx.chromaLumaPower);
}

static void DeserializeRelax(const TextReader& r, Core::RELAXParams& p)
{
    p.denoisingRange = r.Float("denoisingRange", p.denoisingRange);
    p.disocclusionThreshold = r.Float("disocclusionThreshold", p.disocclusionThreshold);
    p.depthThreshold = r.Float("depthThreshold", p.depthThreshold);
    p.specMaxAccumFrames = r.Float("specMaxAccumFrames", p.specMaxAccumFrames);
    p.specMaxFastAccumFrames = r.Float("specMaxFastAccumFrames", p.specMaxFastAccumFrames);
    p.diffMaxAccumFrames = r.Float("diffMaxAccumFrames", p.diffMaxAccumFrames);
    p.diffMaxFastAccumFrames = r.Float("diffMaxFastAccumFrames", p.diffMaxFastAccumFrames);
    p.historyAccelerationAmount = r.Float("historyAccelerationAmount", p.historyAccelerationAmount);
    p.diffBlurRadius = r.Float("diffBlurRadius", p.diffBlurRadius);
    p.specBlurRadius = r.Float("specBlurRadius", p.specBlurRadius);
    p.minHitDistanceWeight = r.Float("minHitDistanceWeight", p.minHitDistanceWeight);
    p.atrousIterations = r.Int("atrousIterations", p.atrousIterations);
    p.lobeAngleFraction = r.Float("lobeAngleFraction", p.lobeAngleFraction);
    p.roughnessFraction = r.Float("roughnessFraction", p.roughnessFraction);
    p.specLobeAngleSlack = r.Float("specLobeAngleSlack", p.specLobeAngleSlack);
    p.specPhiLuminance = r.Float("specPhiLuminance", p.specPhiLuminance);
    p.diffPhiLuminance = r.Float("diffPhiLuminance", p.diffPhiLuminance);
    p.diffMaxLuminanceRelativeDifference = r.Float("diffMaxLuminanceRelativeDifference", p.diffMaxLuminanceRelativeDifference);
    p.specMaxLuminanceRelativeDifference = r.Float("specMaxLuminanceRelativeDifference", p.specMaxLuminanceRelativeDifference);
    p.luminanceEdgeStoppingRelaxation = r.Float("luminanceEdgeStoppingRelaxation", p.luminanceEdgeStoppingRelaxation);
    p.normalEdgeStoppingRelaxation = r.Float("normalEdgeStoppingRelaxation", p.normalEdgeStoppingRelaxation);
    p.roughnessEdgeStoppingRelaxation = r.Float("roughnessEdgeStoppingRelaxation", p.roughnessEdgeStoppingRelaxation);
    p.specVarianceBoost = r.Float("specVarianceBoost", p.specVarianceBoost);
    p.roughnessEdgeStoppingEnabled = r.Bool("roughnessEdgeStoppingEnabled", p.roughnessEdgeStoppingEnabled);
    p.historyFixEdgeStoppingNormalPower = r.Float("historyFixEdgeStoppingNormalPower", p.historyFixEdgeStoppingNormalPower);
    p.historyFixFrameNum = r.Float("historyFixFrameNum", p.historyFixFrameNum);
    p.historyFixBasePixelStride = r.Float("historyFixBasePixelStride", p.historyFixBasePixelStride);
    p.fastHistoryClampingSigmaScale = r.Float("fastHistoryClampingSigmaScale", p.fastHistoryClampingSigmaScale);
    p.historyResetTemporalSigmaScale = r.Float("historyResetTemporalSigmaScale", p.historyResetTemporalSigmaScale);
    p.historyResetSpatialSigmaScale = r.Float("historyResetSpatialSigmaScale", p.historyResetSpatialSigmaScale);
    p.historyResetAmount = r.Float("historyResetAmount", p.historyResetAmount);
    p.enablePrepass = r.Bool("enablePrepass", p.enablePrepass);
    p.enableAntiFirefly = r.Bool("enableAntiFirefly", p.enableAntiFirefly);
    p.bChromaAtrous = r.Bool("bChromaAtrous", p.bChromaAtrous);
    p.chromaAtrousIterations = r.Int("chromaAtrousIterations", p.chromaAtrousIterations);
    p.chromaLumaPower = r.Float("chromaLumaPower", p.chromaLumaPower);
}

static void SerializeReblur(const Core::ReBLURParams& rb, TextWriter& w)
{
    w.Key("denoisingRange", rb.denoisingRange);
    w.Key("disocclusionThreshold", rb.disocclusionThreshold);
    w.Key("disocclusionThresholdAlternate", rb.disocclusionThresholdAlternate);
    w.Key("planeDistanceSensitivity", rb.planeDistanceSensitivity);
    w.Key("lobeAngleFraction", rb.lobeAngleFraction);
    w.Key("roughnessFraction", rb.roughnessFraction);
    w.Key("hitDistA", rb.hitDistA);
    w.Key("hitDistB", rb.hitDistB);
    w.Key("hitDistC", rb.hitDistC);
    w.Key("hitDistD", rb.hitDistD);
    w.Key("maxAccumulatedFrameNum", rb.maxAccumulatedFrameNum);
    w.Key("maxFastAccumulatedFrameNum", rb.maxFastAccumulatedFrameNum);
    w.Key("maxStabilizedFrameNum", rb.maxStabilizedFrameNum);
    w.Key("historyFixFrameNum", rb.historyFixFrameNum);
    w.Key("historyFixBasePixelStride", rb.historyFixBasePixelStride);
    w.Key("fastHistoryClampingSigmaScale", rb.fastHistoryClampingSigmaScale);
    w.Key("diffusePrepassBlurRadius", rb.diffusePrepassBlurRadius);
    w.Key("specularPrepassBlurRadius", rb.specularPrepassBlurRadius);
    w.Key("minBlurRadius", rb.minBlurRadius);
    w.Key("maxBlurRadius", rb.maxBlurRadius);
    w.Key("minHitDistanceWeight", rb.minHitDistanceWeight);
    w.Key("antilagLuminanceSigmaScale", rb.antilagLuminanceSigmaScale);
    w.Key("antilagLuminanceSensitivity", rb.antilagLuminanceSensitivity);
    w.Key("stabilizationStrength", rb.stabilizationStrength);
    w.Key("fireflySuppressorMinRelativeScale", rb.fireflySuppressorMinRelativeScale);
    w.Key("specProbThresholdMvLow", rb.specProbThresholdMvLow);
    w.Key("specProbThresholdMvHigh", rb.specProbThresholdMvHigh);
    w.Key("convergenceS", rb.convergenceS);
    w.Key("convergenceB", rb.convergenceB);
    w.Key("convergenceP", rb.convergenceP);
    w.Key("hitDistanceReconstructionMode", rb.hitDistanceReconstructionMode);
    w.Key("enablePrepass", rb.enablePrepass);
    w.Key("enableAntiFirefly", rb.enableAntiFirefly);
    w.Key("enableStabilizationFireflyCleanup", rb.enableStabilizationFireflyCleanup);
    w.Key("enableTemporalStabilization", rb.enableTemporalStabilization);
    w.Key("bChromaAtrous", rb.bChromaAtrous);
    w.Key("chromaAtrousIterations", rb.chromaAtrousIterations);
    w.Key("chromaLumaPower", rb.chromaLumaPower);
}

static void DeserializeReblur(const TextReader& r, Core::ReBLURParams& p)
{
    p.denoisingRange = r.Float("denoisingRange", p.denoisingRange);
    p.disocclusionThreshold = r.Float("disocclusionThreshold", p.disocclusionThreshold);
    p.disocclusionThresholdAlternate = r.Float("disocclusionThresholdAlternate", p.disocclusionThresholdAlternate);
    p.planeDistanceSensitivity = r.Float("planeDistanceSensitivity", p.planeDistanceSensitivity);
    p.lobeAngleFraction = r.Float("lobeAngleFraction", p.lobeAngleFraction);
    p.roughnessFraction = r.Float("roughnessFraction", p.roughnessFraction);
    p.hitDistA = r.Float("hitDistA", p.hitDistA);
    p.hitDistB = r.Float("hitDistB", p.hitDistB);
    p.hitDistC = r.Float("hitDistC", p.hitDistC);
    p.hitDistD = r.Float("hitDistD", p.hitDistD);
    p.maxAccumulatedFrameNum = r.Float("maxAccumulatedFrameNum", p.maxAccumulatedFrameNum);
    p.maxFastAccumulatedFrameNum = r.Float("maxFastAccumulatedFrameNum", p.maxFastAccumulatedFrameNum);
    p.maxStabilizedFrameNum = r.Float("maxStabilizedFrameNum", p.maxStabilizedFrameNum);
    p.historyFixFrameNum = r.Float("historyFixFrameNum", p.historyFixFrameNum);
    p.historyFixBasePixelStride = r.Float("historyFixBasePixelStride", p.historyFixBasePixelStride);
    p.fastHistoryClampingSigmaScale = r.Float("fastHistoryClampingSigmaScale", p.fastHistoryClampingSigmaScale);
    p.diffusePrepassBlurRadius = r.Float("diffusePrepassBlurRadius", p.diffusePrepassBlurRadius);
    p.specularPrepassBlurRadius = r.Float("specularPrepassBlurRadius", p.specularPrepassBlurRadius);
    p.minBlurRadius = r.Float("minBlurRadius", p.minBlurRadius);
    p.maxBlurRadius = r.Float("maxBlurRadius", p.maxBlurRadius);
    p.minHitDistanceWeight = r.Float("minHitDistanceWeight", p.minHitDistanceWeight);
    p.antilagLuminanceSigmaScale = r.Float("antilagLuminanceSigmaScale", p.antilagLuminanceSigmaScale);
    p.antilagLuminanceSensitivity = r.Float("antilagLuminanceSensitivity", p.antilagLuminanceSensitivity);
    p.stabilizationStrength = r.Float("stabilizationStrength", p.stabilizationStrength);
    p.fireflySuppressorMinRelativeScale = r.Float("fireflySuppressorMinRelativeScale", p.fireflySuppressorMinRelativeScale);
    p.specProbThresholdMvLow = r.Float("specProbThresholdMvLow", p.specProbThresholdMvLow);
    p.specProbThresholdMvHigh = r.Float("specProbThresholdMvHigh", p.specProbThresholdMvHigh);
    p.convergenceS = r.Float("convergenceS", p.convergenceS);
    p.convergenceB = r.Float("convergenceB", p.convergenceB);
    p.convergenceP = r.Float("convergenceP", p.convergenceP);
    p.hitDistanceReconstructionMode = r.Int("hitDistanceReconstructionMode", p.hitDistanceReconstructionMode);
    p.enablePrepass = r.Bool("enablePrepass", p.enablePrepass);
    p.enableAntiFirefly = r.Bool("enableAntiFirefly", p.enableAntiFirefly);
    p.enableStabilizationFireflyCleanup = r.Bool("enableStabilizationFireflyCleanup", p.enableStabilizationFireflyCleanup);
    p.enableTemporalStabilization = r.Bool("enableTemporalStabilization", p.enableTemporalStabilization);
    p.bChromaAtrous = r.Bool("bChromaAtrous", p.bChromaAtrous);
    p.chromaAtrousIterations = r.Int("chromaAtrousIterations", p.chromaAtrousIterations);
    p.chromaLumaPower = r.Float("chromaLumaPower", p.chromaLumaPower);
}

void Serialize(const Core::ReSTIRParams& p, TextWriter& w)
{
    w.Key("spatialPasses", p.spatialPasses);
    w.Key("bPermutationSampling", p.bPermutationSampling);
    w.Key("bAdaptiveSpatial", p.bAdaptiveSpatial);
    w.Key("adaptiveSpatialBoost", p.adaptiveSpatialBoost);
    w.Key("bEnableAntilag", p.bEnableAntilag);
    w.Key("antilagStrength", p.antilagStrength);
    w.Key("spatialRadius", p.spatialRadius);
    w.Key("spatialNeighbors", p.spatialNeighbors);
    w.Key("spatialMCap", p.spatialMCap);
    w.Key("bEnableTemporal", p.bEnableTemporal);
    w.Key("temporalMCap", p.temporalMCap);
    w.Key("bCheckerboard", p.bCheckerboard);
    w.Key("bCheckerboardFullRateResolve", p.bCheckerboardFullRateResolve);
    w.Key("boilingFilterStrength", p.boilingFilterStrength);
    w.Key("bInitialVisibility", p.bInitialVisibility);
    w.Key("bSunLight", p.bSunLight);
    w.Key("regirWClamp", p.regirWClamp);
    w.Key("restirWClamp", p.restirWClamp);
    w.Key("lightProposal", static_cast<uint32_t>(p.lightProposal));
    w.Key("bEmissiveTriangleLights", p.bEmissiveTriangleLights);
    w.Key("emissiveTriRangeMultiplier", p.emissiveTriRangeMultiplier);
    w.Key("emissiveTriMaxPerPrimitive", p.emissiveTriMaxPerPrimitive);
    w.Key("bEnableConfidence", p.bEnableConfidence);
    w.Key("confidenceStrength", p.confidenceStrength);
    w.Key("confidenceSensitivity", p.confidenceSensitivity);
    w.Key("confidenceDarknessBias", p.confidenceDarknessBias);
    w.Key("confidenceHistoryLength", p.confidenceHistoryLength);
    w.Key("confidenceBlurRadius", p.confidenceBlurRadius);
    w.Key("denoiserMode", static_cast<int32_t>(p.denoiserMode));
    w.Key("remodulateOutput", static_cast<int32_t>(p.remodulateOutput));
    w.BeginBlock("atrous");
    w.Key("iterations", p.atrous.iterations);
    w.Key("sigmaLuminance", p.atrous.sigmaLuminance);
    w.Key("sigmaNormal", p.atrous.sigmaNormal);
    w.Key("sigmaDepth", p.atrous.sigmaDepth);
    w.EndBlock();
    w.BeginBlock("svgf");
    w.Key("alphaMin", p.svgf.alphaMin);
    w.Key("gradientThreshold", p.svgf.gradientThreshold);
    w.Key("sigmaLuminance", p.svgf.sigmaLuminance);
    w.Key("sigmaNormal", p.svgf.sigmaNormal);
    w.Key("sigmaDepth", p.svgf.sigmaDepth);
    w.Key("atrousIterations", p.svgf.atrousIterations);
    w.EndBlock();
    w.BeginBlock("relax");
    SerializeRelax(p.relax, w);
    w.EndBlock();
    w.BeginBlock("reblur");
    SerializeReblur(p.reblur, w);
    w.EndBlock();
}

void Deserialize(const TextReader& r, Core::ReSTIRParams& p)
{
    p.spatialPasses = r.UInt("spatialPasses", p.spatialPasses);
    p.bPermutationSampling = r.Bool("bPermutationSampling", p.bPermutationSampling);
    p.bAdaptiveSpatial = r.Bool("bAdaptiveSpatial", p.bAdaptiveSpatial);
    p.adaptiveSpatialBoost = r.Float("adaptiveSpatialBoost", p.adaptiveSpatialBoost);
    p.bEnableAntilag = r.Bool("bEnableAntilag", p.bEnableAntilag);
    p.antilagStrength = r.Float("antilagStrength", p.antilagStrength);
    p.spatialRadius = r.UInt("spatialRadius", p.spatialRadius);
    p.spatialNeighbors = r.UInt("spatialNeighbors", p.spatialNeighbors);
    p.spatialMCap = r.UInt("spatialMCap", p.spatialMCap);
    p.bEnableTemporal = r.Bool("bEnableTemporal", p.bEnableTemporal);
    p.temporalMCap = r.UInt("temporalMCap", p.temporalMCap);
    p.bCheckerboard = r.Bool("bCheckerboard", p.bCheckerboard);
    p.bCheckerboardFullRateResolve = r.Bool("bCheckerboardFullRateResolve", p.bCheckerboardFullRateResolve);
    p.boilingFilterStrength = r.Float("boilingFilterStrength", p.boilingFilterStrength);
    p.bInitialVisibility = r.Bool("bInitialVisibility", p.bInitialVisibility);
    p.bSunLight = r.Bool("bSunLight", p.bSunLight);
    p.regirWClamp = r.Float("regirWClamp", p.regirWClamp);
    p.restirWClamp = r.Float("restirWClamp", p.restirWClamp);
    p.lightProposal = static_cast<Core::ReSTIRParams::LightProposal>(r.UInt("lightProposal", static_cast<uint32_t>(p.lightProposal)));
    p.bEmissiveTriangleLights = r.Bool("bEmissiveTriangleLights", p.bEmissiveTriangleLights);
    p.emissiveTriRangeMultiplier = r.Float("emissiveTriRangeMultiplier", p.emissiveTriRangeMultiplier);
    p.emissiveTriMaxPerPrimitive = r.Int("emissiveTriMaxPerPrimitive", p.emissiveTriMaxPerPrimitive);
    p.bEnableConfidence = r.Bool("bEnableConfidence", p.bEnableConfidence);
    p.confidenceStrength = r.Float("confidenceStrength", p.confidenceStrength);
    p.confidenceSensitivity = r.Float("confidenceSensitivity", p.confidenceSensitivity);
    p.confidenceDarknessBias = r.Float("confidenceDarknessBias", p.confidenceDarknessBias);
    p.confidenceHistoryLength = r.Float("confidenceHistoryLength", p.confidenceHistoryLength);
    p.confidenceBlurRadius = r.UInt("confidenceBlurRadius", p.confidenceBlurRadius);
    p.denoiserMode = static_cast<Core::ReSTIRParams::DenoiserMode>(r.Int("denoiserMode", static_cast<int32_t>(p.denoiserMode)));
    p.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(r.Int("remodulateOutput", static_cast<int32_t>(p.remodulateOutput)));
    const TextReader atrous = r.Block("atrous");
    p.atrous.iterations = atrous.Int("iterations", p.atrous.iterations);
    p.atrous.sigmaLuminance = atrous.Float("sigmaLuminance", p.atrous.sigmaLuminance);
    p.atrous.sigmaNormal = atrous.Float("sigmaNormal", p.atrous.sigmaNormal);
    p.atrous.sigmaDepth = atrous.Float("sigmaDepth", p.atrous.sigmaDepth);
    const TextReader svgf = r.Block("svgf");
    p.svgf.alphaMin = svgf.Float("alphaMin", p.svgf.alphaMin);
    p.svgf.gradientThreshold = svgf.Float("gradientThreshold", p.svgf.gradientThreshold);
    p.svgf.sigmaLuminance = svgf.Float("sigmaLuminance", p.svgf.sigmaLuminance);
    p.svgf.sigmaNormal = svgf.Float("sigmaNormal", p.svgf.sigmaNormal);
    p.svgf.sigmaDepth = svgf.Float("sigmaDepth", p.svgf.sigmaDepth);
    p.svgf.atrousIterations = svgf.Int("atrousIterations", p.svgf.atrousIterations);
    DeserializeRelax(r.Block("relax"), p.relax);
    DeserializeReblur(r.Block("reblur"), p.reblur);
}

void Serialize(const Core::DDGIParams& p, TextWriter& w)
{
    w.Key("bEnabled", p.bEnabled);
    w.Key("probeCountX", p.probeCountX);
    w.Key("probeCountY", p.probeCountY);
    w.Key("probeCountZ", p.probeCountZ);
    w.Key("probeSpacing", p.probeSpacing);
    w.Key("cascadeCount", p.cascadeCount);
    w.Key("edgeBlendCells", p.edgeBlendCells);
    w.Key("bScaleBiasPerCascade", p.bScaleBiasPerCascade);
    w.Key("bLocalVolumes", p.bLocalVolumes);
    w.Key("bCascadeSampling", p.bCascadeSampling);
    w.Key("bWorldVolumeGridCull", p.bWorldVolumeGridCull);
    w.Key("maxResidentWorldVolumes", p.maxResidentWorldVolumes);
    w.Key("worldVolumeWarmupBoost", p.worldVolumeWarmupBoost);
    w.Key("raysPerProbe", p.raysPerProbe);
    w.Key("outerRaysPerProbe", p.outerRaysPerProbe);
    w.Key("bClassification", p.bClassification);
    w.Key("bInfiniteBounce", p.bInfiniteBounce);
    w.Key("bounceIntensity", p.bounceIntensity);
    w.Key("maxRayRadiance", p.maxRayRadiance);
    w.Key("radianceCacheShadeInterval", p.radianceCacheShadeInterval);
    w.Key("radianceCacheAccumCap", p.radianceCacheAccumCap);
    w.Key("hysteresis", p.hysteresis);
    w.Key("visibilityHysteresis", p.visibilityHysteresis);
    w.Key("irradianceGamma", p.irradianceGamma);
    w.Key("irradianceThreshold", p.irradianceThreshold);
    w.Key("brightnessThreshold", p.brightnessThreshold);
    w.Key("distanceExponent", p.distanceExponent);
    w.Key("bApplyToLighting", p.bApplyToLighting);
    w.Key("bFinalGather", p.bFinalGather);
    w.Key("bFinalGatherQuarterRes", p.bFinalGatherQuarterRes);
    w.Key("bFinalGatherDenoise", p.bFinalGatherDenoise);
    w.Key("bFinalGatherChromaDenoise", p.bFinalGatherChromaDenoise);
    w.Key("gatherChromaDenoisePasses", p.gatherChromaDenoisePasses);
    w.Key("gatherChromaLumaPower", p.gatherChromaLumaPower);
    w.Key("bFinalGatherTemporal", p.bFinalGatherTemporal);
    w.Key("bGatherSkipRay", p.bGatherSkipRay);
    w.Key("gatherRaysPerPixel", p.gatherRaysPerPixel);
    w.Key("normalBias", p.normalBias);
    w.Key("viewBias", p.viewBias);
    w.Key("bRelocation", p.bRelocation);
    w.Key("minFrontfaceDistance", p.minFrontfaceDistance);
}

void Deserialize(const TextReader& r, Core::DDGIParams& p)
{
    p.bEnabled = r.Bool("bEnabled", p.bEnabled);
    p.probeCountX = r.Int("probeCountX", p.probeCountX);
    p.probeCountY = r.Int("probeCountY", p.probeCountY);
    p.probeCountZ = r.Int("probeCountZ", p.probeCountZ);
    p.probeSpacing = r.Float("probeSpacing", p.probeSpacing);
    p.cascadeCount = r.UInt("cascadeCount", p.cascadeCount);
    p.edgeBlendCells = r.Float("edgeBlendCells", p.edgeBlendCells);
    p.bScaleBiasPerCascade = r.Bool("bScaleBiasPerCascade", p.bScaleBiasPerCascade);
    p.bLocalVolumes = r.Bool("bLocalVolumes", p.bLocalVolumes);
    p.bCascadeSampling = r.Bool("bCascadeSampling", p.bCascadeSampling);
    p.bWorldVolumeGridCull = r.Bool("bWorldVolumeGridCull", p.bWorldVolumeGridCull);
    p.maxResidentWorldVolumes = r.Int("maxResidentWorldVolumes", p.maxResidentWorldVolumes);
    p.worldVolumeWarmupBoost = r.Int("worldVolumeWarmupBoost", p.worldVolumeWarmupBoost);
    p.raysPerProbe = r.UInt("raysPerProbe", p.raysPerProbe);
    p.outerRaysPerProbe = r.UInt("outerRaysPerProbe", p.outerRaysPerProbe);
    p.bClassification = r.Bool("bClassification", p.bClassification);
    p.bInfiniteBounce = r.Bool("bInfiniteBounce", p.bInfiniteBounce);
    p.bounceIntensity = r.Float("bounceIntensity", p.bounceIntensity);
    p.maxRayRadiance = r.Float("maxRayRadiance", p.maxRayRadiance);
    p.radianceCacheShadeInterval = r.UInt("radianceCacheShadeInterval", p.radianceCacheShadeInterval);
    p.radianceCacheAccumCap = r.UInt("radianceCacheAccumCap", p.radianceCacheAccumCap);
    p.hysteresis = r.Float("hysteresis", p.hysteresis);
    p.visibilityHysteresis = r.Float("visibilityHysteresis", p.visibilityHysteresis);
    p.irradianceGamma = r.Float("irradianceGamma", p.irradianceGamma);
    p.irradianceThreshold = r.Float("irradianceThreshold", p.irradianceThreshold);
    p.brightnessThreshold = r.Float("brightnessThreshold", p.brightnessThreshold);
    p.distanceExponent = r.Float("distanceExponent", p.distanceExponent);
    p.bApplyToLighting = r.Bool("bApplyToLighting", p.bApplyToLighting);
    p.bFinalGather = r.Bool("bFinalGather", p.bFinalGather);
    p.bFinalGatherQuarterRes = r.Bool("bFinalGatherQuarterRes", p.bFinalGatherQuarterRes);
    p.bFinalGatherDenoise = r.Bool("bFinalGatherDenoise", p.bFinalGatherDenoise);
    p.bFinalGatherChromaDenoise = r.Bool("bFinalGatherChromaDenoise", p.bFinalGatherChromaDenoise);
    p.gatherChromaDenoisePasses = r.UInt("gatherChromaDenoisePasses", p.gatherChromaDenoisePasses);
    p.gatherChromaLumaPower = r.Float("gatherChromaLumaPower", p.gatherChromaLumaPower);
    p.bFinalGatherTemporal = r.Bool("bFinalGatherTemporal", p.bFinalGatherTemporal);
    p.gatherRaysPerPixel = r.UInt("gatherRaysPerPixel", p.gatherRaysPerPixel);
    p.bGatherSkipRay = r.Bool("bGatherSkipRay", p.bGatherSkipRay);
    p.normalBias = r.Float("normalBias", p.normalBias);
    p.viewBias = r.Float("viewBias", p.viewBias);
    p.bRelocation = r.Bool("bRelocation", p.bRelocation);
    p.minFrontfaceDistance = r.Float("minFrontfaceDistance", p.minFrontfaceDistance);
}

void Serialize(const Core::ReflectionConfiguration& p, TextWriter& w)
{
    w.Key("bEnabled", p.bEnabled);
    w.Key("bMergedDenoise", p.bMergedDenoise);
    w.Key("bScreenSpaceLighting", p.bScreenSpaceLighting);
    w.Key("bScreenSpaceTrace", p.bScreenSpaceTrace);
    w.Key("bAlphaTest", p.bAlphaTest);
    w.Key("sunMode", static_cast<int32_t>(p.sunMode));
    w.Key("tracedRoughnessMax", p.tracedRoughnessMax);
    w.Key("lightSpecularFromReflectionsMax", p.lightSpecularFromReflectionsMax);
    w.Key("mirrorRoughnessMax", p.mirrorRoughnessMax);
    w.Key("intensity", p.intensity);
    w.Key("maxRayIntensity", p.maxRayIntensity);
    w.Key("ssrThickness", p.ssrThickness);
    w.Key("ssrMaxSteps", p.ssrMaxSteps);
    w.Key("hitLocalShadowRays", p.hitLocalShadowRays);
}

void Deserialize(const TextReader& r, Core::ReflectionConfiguration& p)
{
    p.bEnabled = r.Bool("bEnabled", p.bEnabled);
    p.bMergedDenoise = r.Bool("bMergedDenoise", p.bMergedDenoise);
    p.bScreenSpaceLighting = r.Bool("bScreenSpaceLighting", p.bScreenSpaceLighting);
    p.bScreenSpaceTrace = r.Bool("bScreenSpaceTrace", p.bScreenSpaceTrace);
    p.bAlphaTest = r.Bool("bAlphaTest", p.bAlphaTest);
    const int32_t sunModeRaw = r.Int("sunMode", static_cast<int32_t>(p.sunMode));
    p.sunMode = static_cast<Core::ReflectionConfiguration::SunMode>(sunModeRaw < 0 ? 0 : (sunModeRaw > 2 ? 2 : sunModeRaw));
    p.tracedRoughnessMax = r.Float("tracedRoughnessMax", p.tracedRoughnessMax);
    p.lightSpecularFromReflectionsMax = r.Float("lightSpecularFromReflectionsMax", p.lightSpecularFromReflectionsMax);
    p.mirrorRoughnessMax = r.Float("mirrorRoughnessMax", p.mirrorRoughnessMax);
    p.intensity = r.Float("intensity", p.intensity);
    p.maxRayIntensity = r.Float("maxRayIntensity", p.maxRayIntensity);
    p.ssrThickness = r.Float("ssrThickness", p.ssrThickness);
    p.ssrMaxSteps = r.Int("ssrMaxSteps", p.ssrMaxSteps);
    const int32_t hitLocalShadowRaysRaw = r.Int("hitLocalShadowRays", p.hitLocalShadowRays);
    p.hitLocalShadowRays = hitLocalShadowRaysRaw < 0 ? 0 : (hitLocalShadowRaysRaw > static_cast<int32_t>(REFLECTION_HIT_SHADOW_RAYS_MAX) ? static_cast<int32_t>(REFLECTION_HIT_SHADOW_RAYS_MAX) : hitLocalShadowRaysRaw);
}

void Serialize(const Core::ReflectionProbeConfiguration& p, TextWriter& w)
{
    w.Key("bEnabled", p.bEnabled);
    w.Key("intensity", p.intensity);
    w.Key("bDebugDraw", p.bDebugDraw);
    w.Key("bakedDiffuseClampK", p.bakedDiffuseClampK);
    w.Key("bBruteForcePick", p.bBruteForcePick);
}

void Deserialize(const TextReader& r, Core::ReflectionProbeConfiguration& p)
{
    p.bEnabled = r.Bool("bEnabled", p.bEnabled);
    p.intensity = r.Float("intensity", p.intensity);
    p.bDebugDraw = r.Bool("bDebugDraw", p.bDebugDraw);
    p.bakedDiffuseClampK = r.Float("bakedDiffuseClampK", p.bakedDiffuseClampK);
    p.bBruteForcePick = r.Bool("bBruteForcePick", p.bBruteForcePick);
}

void Serialize(const Core::GTAOConfiguration& p, TextWriter& w)
{
    w.Key("bEnabled", p.bEnabled);
    w.Key("effectRadius", p.effectRadius);
    w.Key("radiusMultiplier", p.radiusMultiplier);
    w.Key("effectFalloffRange", p.effectFalloffRange);
    w.Key("sampleDistributionPower", p.sampleDistributionPower);
    w.Key("thinOccluderCompensation", p.thinOccluderCompensation);
    w.Key("finalValuePower", p.finalValuePower);
    w.Key("depthMipSamplingOffset", p.depthMipSamplingOffset);
    w.Key("sliceCount", p.sliceCount);
    w.Key("stepsPerSlice", p.stepsPerSlice);
    w.Key("denoiseBlurBeta", p.denoiseBlurBeta);
    w.Key("denoisePasses", p.denoisePasses);
}

void Deserialize(const TextReader& r, Core::GTAOConfiguration& p)
{
    p.bEnabled = r.Bool("bEnabled", p.bEnabled);
    p.effectRadius = r.Float("effectRadius", p.effectRadius);
    p.radiusMultiplier = r.Float("radiusMultiplier", p.radiusMultiplier);
    p.effectFalloffRange = r.Float("effectFalloffRange", p.effectFalloffRange);
    p.sampleDistributionPower = r.Float("sampleDistributionPower", p.sampleDistributionPower);
    p.thinOccluderCompensation = r.Float("thinOccluderCompensation", p.thinOccluderCompensation);
    p.finalValuePower = r.Float("finalValuePower", p.finalValuePower);
    p.depthMipSamplingOffset = r.Float("depthMipSamplingOffset", p.depthMipSamplingOffset);
    p.sliceCount = r.Float("sliceCount", p.sliceCount);
    p.stepsPerSlice = r.Float("stepsPerSlice", p.stepsPerSlice);
    p.denoiseBlurBeta = r.Float("denoiseBlurBeta", p.denoiseBlurBeta);
    p.denoisePasses = r.Float("denoisePasses", p.denoisePasses);
}

void Serialize(const Core::SMAAConfiguration& p, TextWriter& w)
{
    w.Key("edgeDetectionMode", static_cast<int32_t>(p.edgeDetectionMode));
    w.Key("threshold", p.threshold);
    w.Key("localContrastAdaptation", p.localContrastAdaptation);
    w.Key("maxSearchSteps", p.maxSearchSteps);
    w.Key("maxSearchStepsDiag", p.maxSearchStepsDiag);
}

void Deserialize(const TextReader& r, Core::SMAAConfiguration& p)
{
    p.edgeDetectionMode = static_cast<Core::SMAAEdgeDetectionMode>(r.Int("edgeDetectionMode", static_cast<int32_t>(p.edgeDetectionMode)));
    p.threshold = r.Float("threshold", p.threshold);
    p.localContrastAdaptation = r.Float("localContrastAdaptation", p.localContrastAdaptation);
    p.maxSearchSteps = r.Int("maxSearchSteps", p.maxSearchSteps);
    p.maxSearchStepsDiag = r.Int("maxSearchStepsDiag", p.maxSearchStepsDiag);
}

void Serialize(const Core::TAAConfiguration& p, TextWriter& w)
{
    w.Key("baseBlendAlpha", p.baseBlendAlpha);
    w.Key("disocclusionThreshold", p.disocclusionThreshold);
    w.Key("varianceGammaLuma", p.varianceGammaLuma);
    w.Key("varianceGammaChroma", p.varianceGammaChroma);
    w.Key("karisStrength", p.karisStrength);
    w.Key("invalidHistoryBlend", p.invalidHistoryBlend);
    w.Key("lumaBoostCap", p.lumaBoostCap);
    w.Key("grazingTurnoverStrength", p.grazingTurnoverStrength);
}

void Deserialize(const TextReader& r, Core::TAAConfiguration& p)
{
    p.baseBlendAlpha = r.Float("baseBlendAlpha", p.baseBlendAlpha);
    p.disocclusionThreshold = r.Float("disocclusionThreshold", p.disocclusionThreshold);
    p.varianceGammaLuma = r.Float("varianceGammaLuma", p.varianceGammaLuma);
    p.varianceGammaChroma = r.Float("varianceGammaChroma", p.varianceGammaChroma);
    p.karisStrength = r.Float("karisStrength", p.karisStrength);
    p.invalidHistoryBlend = r.Float("invalidHistoryBlend", p.invalidHistoryBlend);
    p.lumaBoostCap = r.Float("lumaBoostCap", p.lumaBoostCap);
    p.grazingTurnoverStrength = r.Float("grazingTurnoverStrength", p.grazingTurnoverStrength);
}

void Serialize(const Core::DonutTAAConfiguration& p, TextWriter& w)
{
    w.Key("clampingFactor", p.clampingFactor);
    w.Key("newFrameWeight", p.newFrameWeight);
    w.Key("maxRadiance", p.maxRadiance);
    w.Key("bUseCatmullRom", p.bUseCatmullRom);
    w.Key("bUseHistoryClampRelax", p.bUseHistoryClampRelax);
}

void Deserialize(const TextReader& r, Core::DonutTAAConfiguration& p)
{
    p.clampingFactor = r.Float("clampingFactor", p.clampingFactor);
    p.newFrameWeight = r.Float("newFrameWeight", p.newFrameWeight);
    p.maxRadiance = r.Float("maxRadiance", p.maxRadiance);
    p.bUseCatmullRom = r.Bool("bUseCatmullRom", p.bUseCatmullRom);
    p.bUseHistoryClampRelax = r.Bool("bUseHistoryClampRelax", p.bUseHistoryClampRelax);
}

void Serialize(const Core::AntiAliasingConfiguration& p, TextWriter& w)
{
    w.Key("mode", static_cast<int32_t>(p.mode));
    w.BeginBlock("smaa");
    Serialize(p.smaa, w);
    w.EndBlock();
    w.BeginBlock("taa");
    Serialize(p.taa, w);
    w.EndBlock();
    w.BeginBlock("donutTaa");
    Serialize(p.donutTaa, w);
    w.EndBlock();
}

void Deserialize(const TextReader& r, Core::AntiAliasingConfiguration& p)
{
    p.mode = static_cast<Core::AntiAliasingMode>(r.Int("mode", static_cast<int32_t>(p.mode)));
    Deserialize(r.Block("smaa"), p.smaa);
    Deserialize(r.Block("taa"), p.taa);
    Deserialize(r.Block("donutTaa"), p.donutTaa);
}

void Serialize(const Core::PostProcessConfiguration& p, TextWriter& w)
{
    w.Key("bExposureEnabled", p.bExposureEnabled);
    w.Key("exposureTargetLuminance", p.exposureTargetLuminance);
    w.Key("exposureSpeedBrighten", p.exposureSpeedBrighten);
    w.Key("exposureSpeedDarken", p.exposureSpeedDarken);
    w.Key("exposureMinGainEV", p.exposureMinGainEV);
    w.Key("exposureMaxGainEV", p.exposureMaxGainEV);
    w.Key("exposureLowPercentile", p.exposureLowPercentile);
    w.Key("exposureHighPercentile", p.exposureHighPercentile);
    w.Key("bBloomEnabled", p.bBloomEnabled);
    w.Key("bloomThreshold", p.bloomThreshold);
    w.Key("bloomSoftThreshold", p.bloomSoftThreshold);
    w.Key("bloomRadius", p.bloomRadius);
    w.Key("bloomIntensity", p.bloomIntensity);
    w.Key("bloomClamp", p.bloomClamp);
    w.Key("tonemapOperator", p.tonemapOperator);
    w.Key("uchimuraP", p.uchimuraParams.P);
    w.Key("uchimuraA", p.uchimuraParams.a);
    w.Key("uchimuraM", p.uchimuraParams.m);
    w.Key("uchimuraL", p.uchimuraParams.l);
    w.Key("uchimuraC", p.uchimuraParams.c);
    w.Key("uchimuraB", p.uchimuraParams.b);
    w.Key("hableWhitePoint", p.hableParams.whitePoint);
    w.Key("reinhardWhitePoint", p.reinhardParams.whitePoint);
    w.Key("agxMinEV", p.agxParams.minEV);
    w.Key("agxMaxEV", p.agxParams.maxEV);
    w.Key("khronosStartCompression", p.khronosParams.startCompression);
    w.Key("khronosDesaturation", p.khronosParams.desaturation);
    w.Key("bDepthOfFieldEnabled", p.bDepthOfFieldEnabled);
    w.Key("dofFocusDistance", p.dofFocusDistance);
    w.Key("dofFocusRange", p.dofFocusRange);
    w.Key("dofNearTransition", p.dofNearTransition);
    w.Key("dofFarTransition", p.dofFarTransition);
    w.Key("dofNearRadiusPx", p.dofNearRadiusPx);
    w.Key("dofFarRadiusPx", p.dofFarRadiusPx);
    w.Key("bMotionBlurEnabled", p.bMotionBlurEnabled);
    w.Key("bMotionBlurObjectOnly", p.bMotionBlurObjectOnly);
    w.Key("motionBlurVelocityScale", p.motionBlurVelocityScale);
    w.Key("motionBlurTargetFps", p.motionBlurTargetFps);
    w.Key("motionBlurDepthScale", p.motionBlurDepthScale);
    w.Key("motionBlurMaxRadiusPx", p.motionBlurMaxRadiusPx);
    w.Key("bColorGradingEnabled", p.bColorGradingEnabled);
    w.Key("colorGradingExposure", p.colorGradingExposure);
    w.Key("colorGradingContrast", p.colorGradingContrast);
    w.Key("colorGradingSaturation", p.colorGradingSaturation);
    w.Key("colorGradingTemperature", p.colorGradingTemperature);
    w.Key("colorGradingTint", p.colorGradingTint);
    w.Key("bVignetteEnabled", p.bVignetteEnabled);
    w.Key("vignetteStrength", p.vignetteStrength);
    w.Key("vignetteRadius", p.vignetteRadius);
    w.Key("vignetteSmoothness", p.vignetteSmoothness);
    w.Key("vignetteRoundness", p.vignetteRoundness);
    w.Key("bChromaticAberrationEnabled", p.bChromaticAberrationEnabled);
    w.Key("chromaticAberrationStrength", p.chromaticAberrationStrength);
    w.Key("bSharpeningEnabled", p.bSharpeningEnabled);
    w.Key("sharpeningStrength", p.sharpeningStrength);
    w.Key("bPaniniEnabled", p.bPaniniEnabled);
    w.Key("paniniStrength", p.paniniStrength);
    w.Key("bFilmGrainEnabled", p.bFilmGrainEnabled);
    w.Key("grainStrength", p.grainStrength);
    w.Key("grainSize", p.grainSize);
    w.Key("grainResponse", p.grainResponse);
    w.Key("bDitherEnabled", p.bDitherEnabled);
    w.Key("ditherStrength", p.ditherStrength);
}

void Deserialize(const TextReader& r, Core::PostProcessConfiguration& p)
{
    p.bExposureEnabled = r.Bool("bExposureEnabled", p.bExposureEnabled);
    p.exposureTargetLuminance = r.Float("exposureTargetLuminance", p.exposureTargetLuminance);
    p.exposureSpeedBrighten = r.Float("exposureSpeedBrighten", p.exposureSpeedBrighten);
    p.exposureSpeedDarken = r.Float("exposureSpeedDarken", p.exposureSpeedDarken);
    p.exposureMinGainEV = r.Float("exposureMinGainEV", p.exposureMinGainEV);
    p.exposureMaxGainEV = r.Float("exposureMaxGainEV", p.exposureMaxGainEV);
    p.exposureLowPercentile = r.Float("exposureLowPercentile", p.exposureLowPercentile);
    p.exposureHighPercentile = r.Float("exposureHighPercentile", p.exposureHighPercentile);
    p.bBloomEnabled = r.Bool("bBloomEnabled", p.bBloomEnabled);
    p.bloomThreshold = r.Float("bloomThreshold", p.bloomThreshold);
    p.bloomSoftThreshold = r.Float("bloomSoftThreshold", p.bloomSoftThreshold);
    p.bloomRadius = r.Float("bloomRadius", p.bloomRadius);
    p.bloomIntensity = r.Float("bloomIntensity", p.bloomIntensity);
    p.bloomClamp = r.Float("bloomClamp", p.bloomClamp);
    p.tonemapOperator = r.Int("tonemapOperator", p.tonemapOperator);
    p.uchimuraParams.P = r.Float("uchimuraP", p.uchimuraParams.P);
    p.uchimuraParams.a = r.Float("uchimuraA", p.uchimuraParams.a);
    p.uchimuraParams.m = r.Float("uchimuraM", p.uchimuraParams.m);
    p.uchimuraParams.l = r.Float("uchimuraL", p.uchimuraParams.l);
    p.uchimuraParams.c = r.Float("uchimuraC", p.uchimuraParams.c);
    p.uchimuraParams.b = r.Float("uchimuraB", p.uchimuraParams.b);
    p.hableParams.whitePoint = r.Float("hableWhitePoint", p.hableParams.whitePoint);
    p.reinhardParams.whitePoint = r.Float("reinhardWhitePoint", p.reinhardParams.whitePoint);
    p.agxParams.minEV = r.Float("agxMinEV", p.agxParams.minEV);
    p.agxParams.maxEV = r.Float("agxMaxEV", p.agxParams.maxEV);
    p.khronosParams.startCompression = r.Float("khronosStartCompression", p.khronosParams.startCompression);
    p.khronosParams.desaturation = r.Float("khronosDesaturation", p.khronosParams.desaturation);
    p.bDepthOfFieldEnabled = r.Bool("bDepthOfFieldEnabled", p.bDepthOfFieldEnabled);
    p.dofFocusDistance = r.Float("dofFocusDistance", p.dofFocusDistance);
    p.dofFocusRange = r.Float("dofFocusRange", p.dofFocusRange);
    p.dofNearTransition = r.Float("dofNearTransition", p.dofNearTransition);
    p.dofFarTransition = r.Float("dofFarTransition", p.dofFarTransition);
    p.dofNearRadiusPx = r.Float("dofNearRadiusPx", p.dofNearRadiusPx);
    p.dofFarRadiusPx = r.Float("dofFarRadiusPx", p.dofFarRadiusPx);
    p.bMotionBlurEnabled = r.Bool("bMotionBlurEnabled", p.bMotionBlurEnabled);
    p.bMotionBlurObjectOnly = r.Bool("bMotionBlurObjectOnly", p.bMotionBlurObjectOnly);
    p.motionBlurVelocityScale = r.Float("motionBlurVelocityScale", p.motionBlurVelocityScale);
    p.motionBlurTargetFps = r.Float("motionBlurTargetFps", p.motionBlurTargetFps);
    p.motionBlurDepthScale = r.Float("motionBlurDepthScale", p.motionBlurDepthScale);
    p.motionBlurMaxRadiusPx = r.Float("motionBlurMaxRadiusPx", p.motionBlurMaxRadiusPx);
    p.bColorGradingEnabled = r.Bool("bColorGradingEnabled", p.bColorGradingEnabled);
    p.colorGradingExposure = r.Float("colorGradingExposure", p.colorGradingExposure);
    p.colorGradingContrast = r.Float("colorGradingContrast", p.colorGradingContrast);
    p.colorGradingSaturation = r.Float("colorGradingSaturation", p.colorGradingSaturation);
    p.colorGradingTemperature = r.Float("colorGradingTemperature", p.colorGradingTemperature);
    p.colorGradingTint = r.Float("colorGradingTint", p.colorGradingTint);
    p.bVignetteEnabled = r.Bool("bVignetteEnabled", p.bVignetteEnabled);
    p.vignetteStrength = r.Float("vignetteStrength", p.vignetteStrength);
    p.vignetteRadius = r.Float("vignetteRadius", p.vignetteRadius);
    p.vignetteSmoothness = r.Float("vignetteSmoothness", p.vignetteSmoothness);
    p.vignetteRoundness = r.Float("vignetteRoundness", p.vignetteRoundness);
    p.bChromaticAberrationEnabled = r.Bool("bChromaticAberrationEnabled", p.bChromaticAberrationEnabled);
    p.chromaticAberrationStrength = r.Float("chromaticAberrationStrength", p.chromaticAberrationStrength);
    p.bSharpeningEnabled = r.Bool("bSharpeningEnabled", p.bSharpeningEnabled);
    p.sharpeningStrength = r.Float("sharpeningStrength", p.sharpeningStrength);
    p.bPaniniEnabled = r.Bool("bPaniniEnabled", p.bPaniniEnabled);
    p.paniniStrength = r.Float("paniniStrength", p.paniniStrength);
    p.bFilmGrainEnabled = r.Bool("bFilmGrainEnabled", p.bFilmGrainEnabled);
    p.grainStrength = r.Float("grainStrength", p.grainStrength);
    p.grainSize = r.Float("grainSize", p.grainSize);
    p.grainResponse = r.Float("grainResponse", p.grainResponse);
    p.bDitherEnabled = r.Bool("bDitherEnabled", p.bDitherEnabled);
    p.ditherStrength = r.Float("ditherStrength", p.ditherStrength);
}
}
