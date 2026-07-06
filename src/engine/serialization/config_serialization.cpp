//
// Created by William on 2026-06-13.
//

#include "config_serialization.h"

#include "render/interface/render_interface.h"

namespace Engine::ConfigSerialization
{
static nlohmann::json RelaxToJson(const Core::RELAXParams& rx)
{
    return {
        {"denoisingRange", rx.denoisingRange},
        {"disocclusionThreshold", rx.disocclusionThreshold},
        {"depthThreshold", rx.depthThreshold},
        {"specMaxAccumFrames", rx.specMaxAccumFrames},
        {"specMaxFastAccumFrames", rx.specMaxFastAccumFrames},
        {"diffMaxAccumFrames", rx.diffMaxAccumFrames},
        {"diffMaxFastAccumFrames", rx.diffMaxFastAccumFrames},
        {"historyAccelerationAmount", rx.historyAccelerationAmount},
        {"diffBlurRadius", rx.diffBlurRadius},
        {"specBlurRadius", rx.specBlurRadius},
        {"minHitDistanceWeight", rx.minHitDistanceWeight},
        {"atrousIterations", rx.atrousIterations},
        {"lobeAngleFraction", rx.lobeAngleFraction},
        {"roughnessFraction", rx.roughnessFraction},
        {"specLobeAngleSlack", rx.specLobeAngleSlack},
        {"specPhiLuminance", rx.specPhiLuminance},
        {"diffPhiLuminance", rx.diffPhiLuminance},
        {"diffMaxLuminanceRelativeDifference", rx.diffMaxLuminanceRelativeDifference},
        {"specMaxLuminanceRelativeDifference", rx.specMaxLuminanceRelativeDifference},
        {"luminanceEdgeStoppingRelaxation", rx.luminanceEdgeStoppingRelaxation},
        {"normalEdgeStoppingRelaxation", rx.normalEdgeStoppingRelaxation},
        {"roughnessEdgeStoppingRelaxation", rx.roughnessEdgeStoppingRelaxation},
        {"specVarianceBoost", rx.specVarianceBoost},
        {"roughnessEdgeStoppingEnabled", rx.roughnessEdgeStoppingEnabled},
        {"historyFixEdgeStoppingNormalPower", rx.historyFixEdgeStoppingNormalPower},
        {"historyFixFrameNum", rx.historyFixFrameNum},
        {"historyFixBasePixelStride", rx.historyFixBasePixelStride},
        {"fastHistoryClampingSigmaScale", rx.fastHistoryClampingSigmaScale},
        {"historyResetTemporalSigmaScale", rx.historyResetTemporalSigmaScale},
        {"historyResetSpatialSigmaScale", rx.historyResetSpatialSigmaScale},
        {"historyResetAmount", rx.historyResetAmount},
        {"enablePrepass", rx.enablePrepass},
        {"enableAntiFirefly", rx.enableAntiFirefly},
    };
}

static void RelaxFromJson(const nlohmann::json& r, Core::RELAXParams& p)
{
    auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
    auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };
    auto getFloat = [&](const char* k, float def) { return r.contains(k) && r[k].is_number() ? r[k].get<float>() : def; };

    p.denoisingRange = getFloat("denoisingRange", p.denoisingRange);
    p.disocclusionThreshold = getFloat("disocclusionThreshold", p.disocclusionThreshold);
    p.depthThreshold = getFloat("depthThreshold", p.depthThreshold);
    p.specMaxAccumFrames = getFloat("specMaxAccumFrames", p.specMaxAccumFrames);
    p.specMaxFastAccumFrames = getFloat("specMaxFastAccumFrames", p.specMaxFastAccumFrames);
    p.diffMaxAccumFrames = getFloat("diffMaxAccumFrames", p.diffMaxAccumFrames);
    p.diffMaxFastAccumFrames = getFloat("diffMaxFastAccumFrames", p.diffMaxFastAccumFrames);
    p.historyAccelerationAmount = getFloat("historyAccelerationAmount", p.historyAccelerationAmount);
    p.diffBlurRadius = getFloat("diffBlurRadius", p.diffBlurRadius);
    p.specBlurRadius = getFloat("specBlurRadius", p.specBlurRadius);
    p.minHitDistanceWeight = getFloat("minHitDistanceWeight", p.minHitDistanceWeight);
    p.atrousIterations = getInt("atrousIterations", p.atrousIterations);
    p.lobeAngleFraction = getFloat("lobeAngleFraction", p.lobeAngleFraction);
    p.roughnessFraction = getFloat("roughnessFraction", p.roughnessFraction);
    p.specLobeAngleSlack = getFloat("specLobeAngleSlack", p.specLobeAngleSlack);
    p.specPhiLuminance = getFloat("specPhiLuminance", p.specPhiLuminance);
    p.diffPhiLuminance = getFloat("diffPhiLuminance", p.diffPhiLuminance);
    p.diffMaxLuminanceRelativeDifference = getFloat("diffMaxLuminanceRelativeDifference", p.diffMaxLuminanceRelativeDifference);
    p.specMaxLuminanceRelativeDifference = getFloat("specMaxLuminanceRelativeDifference", p.specMaxLuminanceRelativeDifference);
    p.luminanceEdgeStoppingRelaxation = getFloat("luminanceEdgeStoppingRelaxation", p.luminanceEdgeStoppingRelaxation);
    p.normalEdgeStoppingRelaxation = getFloat("normalEdgeStoppingRelaxation", p.normalEdgeStoppingRelaxation);
    p.roughnessEdgeStoppingRelaxation = getFloat("roughnessEdgeStoppingRelaxation", p.roughnessEdgeStoppingRelaxation);
    p.specVarianceBoost = getFloat("specVarianceBoost", p.specVarianceBoost);
    p.roughnessEdgeStoppingEnabled = getBool("roughnessEdgeStoppingEnabled", p.roughnessEdgeStoppingEnabled);
    p.historyFixEdgeStoppingNormalPower = getFloat("historyFixEdgeStoppingNormalPower", p.historyFixEdgeStoppingNormalPower);
    p.historyFixFrameNum = getFloat("historyFixFrameNum", p.historyFixFrameNum);
    p.historyFixBasePixelStride = getFloat("historyFixBasePixelStride", p.historyFixBasePixelStride);
    p.fastHistoryClampingSigmaScale = getFloat("fastHistoryClampingSigmaScale", p.fastHistoryClampingSigmaScale);
    p.historyResetTemporalSigmaScale = getFloat("historyResetTemporalSigmaScale", p.historyResetTemporalSigmaScale);
    p.historyResetSpatialSigmaScale = getFloat("historyResetSpatialSigmaScale", p.historyResetSpatialSigmaScale);
    p.historyResetAmount = getFloat("historyResetAmount", p.historyResetAmount);
    p.enablePrepass = getBool("enablePrepass", p.enablePrepass);
    p.enableAntiFirefly = getBool("enableAntiFirefly", p.enableAntiFirefly);
}

static nlohmann::json ReblurToJson(const Core::ReBLURParams& rb)
{
    return {
        {"denoisingRange", rb.denoisingRange},
        {"disocclusionThreshold", rb.disocclusionThreshold},
        {"disocclusionThresholdAlternate", rb.disocclusionThresholdAlternate},
        {"planeDistanceSensitivity", rb.planeDistanceSensitivity},
        {"lobeAngleFraction", rb.lobeAngleFraction},
        {"roughnessFraction", rb.roughnessFraction},
        {"hitDistA", rb.hitDistA},
        {"hitDistB", rb.hitDistB},
        {"hitDistC", rb.hitDistC},
        {"hitDistD", rb.hitDistD},
        {"maxAccumulatedFrameNum", rb.maxAccumulatedFrameNum},
        {"maxFastAccumulatedFrameNum", rb.maxFastAccumulatedFrameNum},
        {"maxStabilizedFrameNum", rb.maxStabilizedFrameNum},
        {"historyFixFrameNum", rb.historyFixFrameNum},
        {"historyFixBasePixelStride", rb.historyFixBasePixelStride},
        {"fastHistoryClampingSigmaScale", rb.fastHistoryClampingSigmaScale},
        {"diffusePrepassBlurRadius", rb.diffusePrepassBlurRadius},
        {"specularPrepassBlurRadius", rb.specularPrepassBlurRadius},
        {"minBlurRadius", rb.minBlurRadius},
        {"maxBlurRadius", rb.maxBlurRadius},
        {"minHitDistanceWeight", rb.minHitDistanceWeight},
        {"antilagLuminanceSigmaScale", rb.antilagLuminanceSigmaScale},
        {"antilagLuminanceSensitivity", rb.antilagLuminanceSensitivity},
        {"stabilizationStrength", rb.stabilizationStrength},
        {"fireflySuppressorMinRelativeScale", rb.fireflySuppressorMinRelativeScale},
        {"specProbThresholdMvLow", rb.specProbThresholdMvLow},
        {"specProbThresholdMvHigh", rb.specProbThresholdMvHigh},
        {"convergenceS", rb.convergenceS},
        {"convergenceB", rb.convergenceB},
        {"convergenceP", rb.convergenceP},
        {"hitDistanceReconstructionMode", rb.hitDistanceReconstructionMode},
        {"enablePrepass", rb.enablePrepass},
        {"enableAntiFirefly", rb.enableAntiFirefly},
        {"enableStabilizationFireflyCleanup", rb.enableStabilizationFireflyCleanup},
        {"enableTemporalStabilization", rb.enableTemporalStabilization},
    };
}

static void ReblurFromJson(const nlohmann::json& r, Core::ReBLURParams& p)
{
    auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
    auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };
    auto getFloat = [&](const char* k, float def) { return r.contains(k) && r[k].is_number() ? r[k].get<float>() : def; };

    p.denoisingRange = getFloat("denoisingRange", p.denoisingRange);
    p.disocclusionThreshold = getFloat("disocclusionThreshold", p.disocclusionThreshold);
    p.disocclusionThresholdAlternate = getFloat("disocclusionThresholdAlternate", p.disocclusionThresholdAlternate);
    p.planeDistanceSensitivity = getFloat("planeDistanceSensitivity", p.planeDistanceSensitivity);
    p.lobeAngleFraction = getFloat("lobeAngleFraction", p.lobeAngleFraction);
    p.roughnessFraction = getFloat("roughnessFraction", p.roughnessFraction);
    p.hitDistA = getFloat("hitDistA", p.hitDistA);
    p.hitDistB = getFloat("hitDistB", p.hitDistB);
    p.hitDistC = getFloat("hitDistC", p.hitDistC);
    p.hitDistD = getFloat("hitDistD", p.hitDistD);
    p.maxAccumulatedFrameNum = getFloat("maxAccumulatedFrameNum", p.maxAccumulatedFrameNum);
    p.maxFastAccumulatedFrameNum = getFloat("maxFastAccumulatedFrameNum", p.maxFastAccumulatedFrameNum);
    p.maxStabilizedFrameNum = getFloat("maxStabilizedFrameNum", p.maxStabilizedFrameNum);
    p.historyFixFrameNum = getFloat("historyFixFrameNum", p.historyFixFrameNum);
    p.historyFixBasePixelStride = getFloat("historyFixBasePixelStride", p.historyFixBasePixelStride);
    p.fastHistoryClampingSigmaScale = getFloat("fastHistoryClampingSigmaScale", p.fastHistoryClampingSigmaScale);
    p.diffusePrepassBlurRadius = getFloat("diffusePrepassBlurRadius", p.diffusePrepassBlurRadius);
    p.specularPrepassBlurRadius = getFloat("specularPrepassBlurRadius", p.specularPrepassBlurRadius);
    p.minBlurRadius = getFloat("minBlurRadius", p.minBlurRadius);
    p.maxBlurRadius = getFloat("maxBlurRadius", p.maxBlurRadius);
    p.minHitDistanceWeight = getFloat("minHitDistanceWeight", p.minHitDistanceWeight);
    p.antilagLuminanceSigmaScale = getFloat("antilagLuminanceSigmaScale", p.antilagLuminanceSigmaScale);
    p.antilagLuminanceSensitivity = getFloat("antilagLuminanceSensitivity", p.antilagLuminanceSensitivity);
    p.stabilizationStrength = getFloat("stabilizationStrength", p.stabilizationStrength);
    p.fireflySuppressorMinRelativeScale = getFloat("fireflySuppressorMinRelativeScale", p.fireflySuppressorMinRelativeScale);
    p.specProbThresholdMvLow = getFloat("specProbThresholdMvLow", p.specProbThresholdMvLow);
    p.specProbThresholdMvHigh = getFloat("specProbThresholdMvHigh", p.specProbThresholdMvHigh);
    p.convergenceS = getFloat("convergenceS", p.convergenceS);
    p.convergenceB = getFloat("convergenceB", p.convergenceB);
    p.convergenceP = getFloat("convergenceP", p.convergenceP);
    p.hitDistanceReconstructionMode = getInt("hitDistanceReconstructionMode", p.hitDistanceReconstructionMode);
    p.enablePrepass = getBool("enablePrepass", p.enablePrepass);
    p.enableAntiFirefly = getBool("enableAntiFirefly", p.enableAntiFirefly);
    p.enableStabilizationFireflyCleanup = getBool("enableStabilizationFireflyCleanup", p.enableStabilizationFireflyCleanup);
    p.enableTemporalStabilization = getBool("enableTemporalStabilization", p.enableTemporalStabilization);
}

nlohmann::json ToJson(const Core::ReSTIRParams& p)
{
    return {
        {"spatialPasses", p.spatialPasses},
        {"bPermutationSampling", p.bPermutationSampling},
        {"bAdaptiveSpatial", p.bAdaptiveSpatial},
        {"adaptiveSpatialBoost", p.adaptiveSpatialBoost},
        {"bEnableAntilag", p.bEnableAntilag},
        {"antilagStrength", p.antilagStrength},
        {"spatialRadius", p.spatialRadius},
        {"spatialNeighbors", p.spatialNeighbors},
        {"spatialMCap", p.spatialMCap},
        {"bEnableTemporal", p.bEnableTemporal},
        {"temporalMCap", p.temporalMCap},
        {"bCheckerboard", p.bCheckerboard},
        {"boilingFilterStrength", p.boilingFilterStrength},
        {"bInitialVisibility", p.bInitialVisibility},
        {"bSunCandidateVisibility", p.bSunCandidateVisibility},
        {"regirWClamp", p.regirWClamp},
        {"restirWClamp", p.restirWClamp},
        {"bEnableConfidence", p.bEnableConfidence},
        {"confidenceStrength", p.confidenceStrength},
        {"confidenceSensitivity", p.confidenceSensitivity},
        {"confidenceDarknessBias", p.confidenceDarknessBias},
        {"confidenceHistoryLength", p.confidenceHistoryLength},
        {"confidenceBlurRadius", p.confidenceBlurRadius},
        {"denoiserMode", static_cast<int32_t>(p.denoiserMode)},
        {"remodulateOutput", static_cast<int32_t>(p.remodulateOutput)},
        {"atrous", nlohmann::json{{"iterations", p.atrous.iterations}, {"sigmaLuminance", p.atrous.sigmaLuminance}, {"sigmaNormal", p.atrous.sigmaNormal}, {"sigmaDepth", p.atrous.sigmaDepth}}},
        {"svgf", nlohmann::json{{"alphaMin", p.svgf.alphaMin}, {"gradientThreshold", p.svgf.gradientThreshold}, {"sigmaLuminance", p.svgf.sigmaLuminance}, {"sigmaNormal", p.svgf.sigmaNormal}, {"sigmaDepth", p.svgf.sigmaDepth}, {"atrousIterations", p.svgf.atrousIterations}}},
        {"relax", RelaxToJson(p.relax)},
        {"reblur", ReblurToJson(p.reblur)},
    };
}

void FromJson(const nlohmann::json& r, Core::ReSTIRParams& p)
{
    auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
    auto getUint = [&](const char* k, uint32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<uint32_t>() : def; };
    auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };

    p.spatialPasses = getUint("spatialPasses", getBool("bSpatial2", false) ? 2u : p.spatialPasses);
    p.bPermutationSampling = getBool("bPermutationSampling", p.bPermutationSampling);
    p.bAdaptiveSpatial = getBool("bAdaptiveSpatial", p.bAdaptiveSpatial);
    p.adaptiveSpatialBoost = r.contains("adaptiveSpatialBoost") && r["adaptiveSpatialBoost"].is_number() ? r["adaptiveSpatialBoost"].get<float>() : p.adaptiveSpatialBoost;
    p.bEnableAntilag = getBool("bEnableAntilag", p.bEnableAntilag);
    p.antilagStrength = r.contains("antilagStrength") && r["antilagStrength"].is_number() ? r["antilagStrength"].get<float>() : p.antilagStrength;
    p.spatialRadius = getUint("spatialRadius", p.spatialRadius);
    p.spatialNeighbors = getUint("spatialNeighbors", p.spatialNeighbors);
    p.spatialMCap = getUint("spatialMCap", p.spatialMCap);
    p.bEnableTemporal = getBool("bEnableTemporal", p.bEnableTemporal);
    p.temporalMCap = getUint("temporalMCap", p.temporalMCap);
    p.bCheckerboard = getBool("bCheckerboard", p.bCheckerboard);
    p.boilingFilterStrength = r.contains("boilingFilterStrength") && r["boilingFilterStrength"].is_number() ? r["boilingFilterStrength"].get<float>() : p.boilingFilterStrength;
    p.bInitialVisibility = getBool("bInitialVisibility", p.bInitialVisibility);
    p.bSunCandidateVisibility = getBool("bSunCandidateVisibility", p.bSunCandidateVisibility);
    p.regirWClamp = r.contains("regirWClamp") && r["regirWClamp"].is_number() ? r["regirWClamp"].get<float>() : p.regirWClamp;
    p.restirWClamp = r.contains("restirWClamp") && r["restirWClamp"].is_number() ? r["restirWClamp"].get<float>() : p.restirWClamp;
    p.bEnableConfidence = getBool("bEnableConfidence", p.bEnableConfidence);
    p.confidenceStrength = r.contains("confidenceStrength") && r["confidenceStrength"].is_number() ? r["confidenceStrength"].get<float>() : p.confidenceStrength;
    p.confidenceSensitivity = r.contains("confidenceSensitivity") && r["confidenceSensitivity"].is_number() ? r["confidenceSensitivity"].get<float>() : p.confidenceSensitivity;
    p.confidenceDarknessBias = r.contains("confidenceDarknessBias") && r["confidenceDarknessBias"].is_number() ? r["confidenceDarknessBias"].get<float>() : p.confidenceDarknessBias;
    p.confidenceHistoryLength = r.contains("confidenceHistoryLength") && r["confidenceHistoryLength"].is_number() ? r["confidenceHistoryLength"].get<float>() : p.confidenceHistoryLength;
    p.confidenceBlurRadius = getUint("confidenceBlurRadius", p.confidenceBlurRadius);
    p.denoiserMode = static_cast<Core::ReSTIRParams::DenoiserMode>(getInt("denoiserMode", static_cast<int32_t>(p.denoiserMode)));
    p.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(getInt("remodulateOutput", static_cast<int32_t>(p.remodulateOutput)));
    if (r.contains("atrous") && r["atrous"].is_object()) {
        const auto& o = r["atrous"];
        auto oInt = [&](const char* k, int32_t def) { return o.contains(k) && o[k].is_number() ? o[k].get<int32_t>() : def; };
        auto oFloat = [&](const char* k, float def) { return o.contains(k) && o[k].is_number() ? o[k].get<float>() : def; };
        p.atrous.iterations = oInt("iterations", p.atrous.iterations);
        p.atrous.sigmaLuminance = oFloat("sigmaLuminance", p.atrous.sigmaLuminance);
        p.atrous.sigmaNormal = oFloat("sigmaNormal", p.atrous.sigmaNormal);
        p.atrous.sigmaDepth = oFloat("sigmaDepth", p.atrous.sigmaDepth);
    }
    if (r.contains("svgf") && r["svgf"].is_object()) {
        const auto& o = r["svgf"];
        auto oInt = [&](const char* k, int32_t def) { return o.contains(k) && o[k].is_number() ? o[k].get<int32_t>() : def; };
        auto oFloat = [&](const char* k, float def) { return o.contains(k) && o[k].is_number() ? o[k].get<float>() : def; };
        p.svgf.alphaMin = oFloat("alphaMin", p.svgf.alphaMin);
        p.svgf.gradientThreshold = oFloat("gradientThreshold", p.svgf.gradientThreshold);
        p.svgf.sigmaLuminance = oFloat("sigmaLuminance", p.svgf.sigmaLuminance);
        p.svgf.sigmaNormal = oFloat("sigmaNormal", p.svgf.sigmaNormal);
        p.svgf.sigmaDepth = oFloat("sigmaDepth", p.svgf.sigmaDepth);
        p.svgf.atrousIterations = oInt("atrousIterations", p.svgf.atrousIterations);
    }
    if (r.contains("relax") && r["relax"].is_object()) {
        RelaxFromJson(r["relax"], p.relax);
    }
    if (r.contains("reblur") && r["reblur"].is_object()) {
        ReblurFromJson(r["reblur"], p.reblur);
    }
}

nlohmann::json ToJson(const Core::DDGIParams& p)
{
    return {
        {"bEnabled", p.bEnabled},
        {"probeCountX", p.probeCountX},
        {"probeCountY", p.probeCountY},
        {"probeCountZ", p.probeCountZ},
        {"probeSpacing", p.probeSpacing},
        {"raysPerProbe", p.raysPerProbe},
        {"hysteresis", p.hysteresis},
        {"irradianceGamma", p.irradianceGamma},
        {"irradianceThreshold", p.irradianceThreshold},
        {"brightnessThreshold", p.brightnessThreshold},
    };
}

void FromJson(const nlohmann::json& d, Core::DDGIParams& p)
{
    auto dBool = [&](const char* k, bool def) { return d.contains(k) && d[k].is_boolean() ? d[k].get<bool>() : def; };
    auto dInt = [&](const char* k, int32_t def) { return d.contains(k) && d[k].is_number() ? d[k].get<int32_t>() : def; };
    auto dUint = [&](const char* k, uint32_t def) { return d.contains(k) && d[k].is_number() ? d[k].get<uint32_t>() : def; };
    auto dFloat = [&](const char* k, float def) { return d.contains(k) && d[k].is_number() ? d[k].get<float>() : def; };

    p.bEnabled = dBool("bEnabled", p.bEnabled);
    p.probeCountX = dInt("probeCountX", p.probeCountX);
    p.probeCountY = dInt("probeCountY", p.probeCountY);
    p.probeCountZ = dInt("probeCountZ", p.probeCountZ);
    p.probeSpacing = dFloat("probeSpacing", p.probeSpacing);
    p.raysPerProbe = dUint("raysPerProbe", p.raysPerProbe);
    p.hysteresis = dFloat("hysteresis", p.hysteresis);
    p.irradianceGamma = dFloat("irradianceGamma", p.irradianceGamma);
    p.irradianceThreshold = dFloat("irradianceThreshold", p.irradianceThreshold);
    p.brightnessThreshold = dFloat("brightnessThreshold", p.brightnessThreshold);
}

nlohmann::json ToJson(const Core::GTAOConfiguration& p)
{
    return {
        {"bEnabled", p.bEnabled},
        {"effectRadius", p.effectRadius},
        {"radiusMultiplier", p.radiusMultiplier},
        {"effectFalloffRange", p.effectFalloffRange},
        {"sampleDistributionPower", p.sampleDistributionPower},
        {"thinOccluderCompensation", p.thinOccluderCompensation},
        {"finalValuePower", p.finalValuePower},
        {"depthMipSamplingOffset", p.depthMipSamplingOffset},
        {"sliceCount", p.sliceCount},
        {"stepsPerSlice", p.stepsPerSlice},
        {"denoiseBlurBeta", p.denoiseBlurBeta},
    };
}

void FromJson(const nlohmann::json& g, Core::GTAOConfiguration& p)
{
    auto gBool = [&](const char* k, bool def) { return g.contains(k) && g[k].is_boolean() ? g[k].get<bool>() : def; };
    auto gFloat = [&](const char* k, float def) { return g.contains(k) && g[k].is_number() ? g[k].get<float>() : def; };
    p.bEnabled = gBool("bEnabled", p.bEnabled);
    p.effectRadius = gFloat("effectRadius", p.effectRadius);
    p.radiusMultiplier = gFloat("radiusMultiplier", p.radiusMultiplier);
    p.effectFalloffRange = gFloat("effectFalloffRange", p.effectFalloffRange);
    p.sampleDistributionPower = gFloat("sampleDistributionPower", p.sampleDistributionPower);
    p.thinOccluderCompensation = gFloat("thinOccluderCompensation", p.thinOccluderCompensation);
    p.finalValuePower = gFloat("finalValuePower", p.finalValuePower);
    p.depthMipSamplingOffset = gFloat("depthMipSamplingOffset", p.depthMipSamplingOffset);
    p.sliceCount = gFloat("sliceCount", p.sliceCount);
    p.stepsPerSlice = gFloat("stepsPerSlice", p.stepsPerSlice);
    p.denoiseBlurBeta = gFloat("denoiseBlurBeta", p.denoiseBlurBeta);
}

nlohmann::json ToJson(const Core::SMAAConfiguration& p)
{
    return {
        {"edgeDetectionMode", static_cast<int32_t>(p.edgeDetectionMode)},
        {"threshold", p.threshold},
        {"localContrastAdaptation", p.localContrastAdaptation},
        {"maxSearchSteps", p.maxSearchSteps},
        {"maxSearchStepsDiag", p.maxSearchStepsDiag},
    };
}

void FromJson(const nlohmann::json& s, Core::SMAAConfiguration& p)
{
    auto sInt = [&](const char* k, int32_t def) { return s.contains(k) && s[k].is_number() ? s[k].get<int32_t>() : def; };
    auto sFloat = [&](const char* k, float def) { return s.contains(k) && s[k].is_number() ? s[k].get<float>() : def; };
    p.edgeDetectionMode = static_cast<Core::SMAAEdgeDetectionMode>(sInt("edgeDetectionMode", static_cast<int32_t>(p.edgeDetectionMode)));
    p.threshold = sFloat("threshold", p.threshold);
    p.localContrastAdaptation = sFloat("localContrastAdaptation", p.localContrastAdaptation);
    p.maxSearchSteps = sInt("maxSearchSteps", p.maxSearchSteps);
    p.maxSearchStepsDiag = sInt("maxSearchStepsDiag", p.maxSearchStepsDiag);
}

nlohmann::json ToJson(const Core::TAAConfiguration& p)
{
    return {
        {"baseBlendAlpha", p.baseBlendAlpha},
        {"disocclusionThreshold", p.disocclusionThreshold},
        {"varianceGammaLuma", p.varianceGammaLuma},
        {"varianceGammaChroma", p.varianceGammaChroma},
        {"karisStrength", p.karisStrength},
        {"invalidHistoryBlend", p.invalidHistoryBlend},
        {"lumaBoostCap", p.lumaBoostCap},
        {"grazingTurnoverStrength", p.grazingTurnoverStrength},
    };
}

void FromJson(const nlohmann::json& t, Core::TAAConfiguration& p)
{
    auto tFloat = [&](const char* k, float def) { return t.contains(k) && t[k].is_number() ? t[k].get<float>() : def; };
    p.baseBlendAlpha = tFloat("baseBlendAlpha", p.baseBlendAlpha);
    p.disocclusionThreshold = tFloat("disocclusionThreshold", p.disocclusionThreshold);
    p.varianceGammaLuma = tFloat("varianceGammaLuma", p.varianceGammaLuma);
    p.varianceGammaChroma = tFloat("varianceGammaChroma", p.varianceGammaChroma);
    p.karisStrength = tFloat("karisStrength", p.karisStrength);
    p.invalidHistoryBlend = tFloat("invalidHistoryBlend", p.invalidHistoryBlend);
    p.lumaBoostCap = tFloat("lumaBoostCap", p.lumaBoostCap);
    p.grazingTurnoverStrength = tFloat("grazingTurnoverStrength", p.grazingTurnoverStrength);
}

nlohmann::json ToJson(const Core::DonutTAAConfiguration& p)
{
    return {
        {"clampingFactor", p.clampingFactor},
        {"newFrameWeight", p.newFrameWeight},
        {"maxRadiance", p.maxRadiance},
        {"bUseCatmullRom", p.bUseCatmullRom},
        {"bUseHistoryClampRelax", p.bUseHistoryClampRelax},
    };
}

void FromJson(const nlohmann::json& t, Core::DonutTAAConfiguration& p)
{
    auto tFloat = [&](const char* k, float def) { return t.contains(k) && t[k].is_number() ? t[k].get<float>() : def; };
    auto tBool = [&](const char* k, bool def) { return t.contains(k) && t[k].is_boolean() ? t[k].get<bool>() : def; };
    p.clampingFactor = tFloat("clampingFactor", p.clampingFactor);
    p.newFrameWeight = tFloat("newFrameWeight", p.newFrameWeight);
    p.maxRadiance = tFloat("maxRadiance", p.maxRadiance);
    p.bUseCatmullRom = tBool("bUseCatmullRom", p.bUseCatmullRom);
    p.bUseHistoryClampRelax = tBool("bUseHistoryClampRelax", p.bUseHistoryClampRelax);
}

nlohmann::json ToJson(const Core::AntiAliasingConfiguration& p)
{
    return {
        {"mode", static_cast<int32_t>(p.mode)},
        {"smaa", ToJson(p.smaa)},
        {"taa", ToJson(p.taa)},
        {"donutTaa", ToJson(p.donutTaa)},
    };
}

void FromJson(const nlohmann::json& j, Core::AntiAliasingConfiguration& p)
{
    if (j.contains("mode") && j["mode"].is_number()) {
        p.mode = static_cast<Core::AntiAliasingMode>(j["mode"].get<int32_t>());
    }
    if (j.contains("smaa") && j["smaa"].is_object()) {
        FromJson(j["smaa"], p.smaa);
    }
    if (j.contains("taa") && j["taa"].is_object()) {
        FromJson(j["taa"], p.taa);
    }
    if (j.contains("donutTaa") && j["donutTaa"].is_object()) {
        FromJson(j["donutTaa"], p.donutTaa);
    }
}

nlohmann::json ToJson(const Core::PostProcessConfiguration& p)
{
    return {
        {"bExposureEnabled", p.bExposureEnabled},
        {"exposureTargetLuminance", p.exposureTargetLuminance},
        {"exposureAdaptationRate", p.exposureAdaptationRate},
        {"bBloomEnabled", p.bBloomEnabled},
        {"bloomThreshold", p.bloomThreshold},
        {"bloomSoftThreshold", p.bloomSoftThreshold},
        {"bloomRadius", p.bloomRadius},
        {"bloomIntensity", p.bloomIntensity},
        {"bloomClamp", p.bloomClamp},
        {"tonemapOperator", p.tonemapOperator},
        {"uchimuraP", p.uchimuraParams.P},
        {"uchimuraA", p.uchimuraParams.a},
        {"uchimuraM", p.uchimuraParams.m},
        {"uchimuraL", p.uchimuraParams.l},
        {"uchimuraC", p.uchimuraParams.c},
        {"uchimuraB", p.uchimuraParams.b},
        {"hableWhitePoint", p.hableParams.whitePoint},
        {"reinhardWhitePoint", p.reinhardParams.whitePoint},
        {"agxMinEV", p.agxParams.minEV},
        {"agxMaxEV", p.agxParams.maxEV},
        {"khronosStartCompression", p.khronosParams.startCompression},
        {"khronosDesaturation", p.khronosParams.desaturation},
        {"motionBlurVelocityScale", p.motionBlurVelocityScale},
        {"motionBlurDepthScale", p.motionBlurDepthScale},
        {"bColorGradingEnabled", p.bColorGradingEnabled},
        {"colorGradingExposure", p.colorGradingExposure},
        {"colorGradingContrast", p.colorGradingContrast},
        {"colorGradingSaturation", p.colorGradingSaturation},
        {"colorGradingTemperature", p.colorGradingTemperature},
        {"colorGradingTint", p.colorGradingTint},
        {"bVignetteAberrationEnabled", p.bVignetteAberrationEnabled},
        {"chromaticAberrationStrength", p.chromaticAberrationStrength},
        {"vignetteStrength", p.vignetteStrength},
        {"vignetteRadius", p.vignetteRadius},
        {"vignetteSmoothness", p.vignetteSmoothness},
        {"bSharpeningEnabled", p.bSharpeningEnabled},
        {"sharpeningStrength", p.sharpeningStrength},
        {"bPaniniEnabled", p.bPaniniEnabled},
        {"paniniStrength", p.paniniStrength},
        {"bFilmGrainEnabled", p.bFilmGrainEnabled},
        {"grainStrength", p.grainStrength},
        {"grainSize", p.grainSize},
        {"bDitherEnabled", p.bDitherEnabled},
        {"ditherStrength", p.ditherStrength},
    };
}

void FromJson(const nlohmann::json& r, Core::PostProcessConfiguration& p)
{
    auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
    auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };
    auto getFloat = [&](const char* k, float def) { return r.contains(k) && r[k].is_number() ? r[k].get<float>() : def; };

    p.bExposureEnabled = getBool("bExposureEnabled", p.bExposureEnabled);
    p.exposureTargetLuminance = getFloat("exposureTargetLuminance", p.exposureTargetLuminance);
    p.exposureAdaptationRate = getFloat("exposureAdaptationRate", p.exposureAdaptationRate);
    p.bBloomEnabled = getBool("bBloomEnabled", p.bBloomEnabled);
    p.bloomThreshold = getFloat("bloomThreshold", p.bloomThreshold);
    p.bloomSoftThreshold = getFloat("bloomSoftThreshold", p.bloomSoftThreshold);
    p.bloomRadius = getFloat("bloomRadius", p.bloomRadius);
    p.bloomIntensity = getFloat("bloomIntensity", p.bloomIntensity);
    p.bloomClamp = getFloat("bloomClamp", p.bloomClamp);
    p.tonemapOperator = getInt("tonemapOperator", p.tonemapOperator);
    p.uchimuraParams.P = getFloat("uchimuraP", p.uchimuraParams.P);
    p.uchimuraParams.a = getFloat("uchimuraA", p.uchimuraParams.a);
    p.uchimuraParams.m = getFloat("uchimuraM", p.uchimuraParams.m);
    p.uchimuraParams.l = getFloat("uchimuraL", p.uchimuraParams.l);
    p.uchimuraParams.c = getFloat("uchimuraC", p.uchimuraParams.c);
    p.uchimuraParams.b = getFloat("uchimuraB", p.uchimuraParams.b);
    p.hableParams.whitePoint = getFloat("hableWhitePoint", p.hableParams.whitePoint);
    p.reinhardParams.whitePoint = getFloat("reinhardWhitePoint", p.reinhardParams.whitePoint);
    p.agxParams.minEV = getFloat("agxMinEV", p.agxParams.minEV);
    p.agxParams.maxEV = getFloat("agxMaxEV", p.agxParams.maxEV);
    p.khronosParams.startCompression = getFloat("khronosStartCompression", p.khronosParams.startCompression);
    p.khronosParams.desaturation = getFloat("khronosDesaturation", p.khronosParams.desaturation);
    p.motionBlurVelocityScale = getFloat("motionBlurVelocityScale", p.motionBlurVelocityScale);
    p.motionBlurDepthScale = getFloat("motionBlurDepthScale", p.motionBlurDepthScale);
    p.bColorGradingEnabled = getBool("bColorGradingEnabled", p.bColorGradingEnabled);
    p.colorGradingExposure = getFloat("colorGradingExposure", p.colorGradingExposure);
    p.colorGradingContrast = getFloat("colorGradingContrast", p.colorGradingContrast);
    p.colorGradingSaturation = getFloat("colorGradingSaturation", p.colorGradingSaturation);
    p.colorGradingTemperature = getFloat("colorGradingTemperature", p.colorGradingTemperature);
    p.colorGradingTint = getFloat("colorGradingTint", p.colorGradingTint);
    p.bVignetteAberrationEnabled = getBool("bVignetteAberrationEnabled", p.bVignetteAberrationEnabled);
    p.chromaticAberrationStrength = getFloat("chromaticAberrationStrength", p.chromaticAberrationStrength);
    p.vignetteStrength = getFloat("vignetteStrength", p.vignetteStrength);
    p.vignetteRadius = getFloat("vignetteRadius", p.vignetteRadius);
    p.vignetteSmoothness = getFloat("vignetteSmoothness", p.vignetteSmoothness);
    p.bSharpeningEnabled = getBool("bSharpeningEnabled", p.bSharpeningEnabled);
    p.sharpeningStrength = getFloat("sharpeningStrength", p.sharpeningStrength);
    p.bPaniniEnabled = getBool("bPaniniEnabled", p.bPaniniEnabled);
    p.paniniStrength = getFloat("paniniStrength", p.paniniStrength);
    p.bFilmGrainEnabled = getBool("bFilmGrainEnabled", p.bFilmGrainEnabled);
    p.grainStrength = getFloat("grainStrength", p.grainStrength);
    p.grainSize = getFloat("grainSize", p.grainSize);
    p.bDitherEnabled = getBool("bDitherEnabled", p.bDitherEnabled);
    p.ditherStrength = getFloat("ditherStrength", p.ditherStrength);
}
}
