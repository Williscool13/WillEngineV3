//
// Created by William on 2026-04-22.
//

#include "project_config.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "platform/paths.h"

namespace Engine
{
static Core::Path GetProjectConfigPath()
{
    return Platform::GetConfigPath() / "project.wconfig";
}

ProjectConfig ReadProjectConfig()
{
    ProjectConfig config{};

    const Core::Path path = GetProjectConfigPath();
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return config;
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded()) {
        return config;
    }

    if (j.contains("defaultScene") && j["defaultScene"].is_string()) {
        config.defaultScene = Core::InlineString<256>(j["defaultScene"].get<std::string_view>());
    }

    if (j.contains("bAutoSave") && j["bAutoSave"].is_boolean()) {
        config.bAutoSave = j["bAutoSave"].get<bool>();
    }

    if (j.contains("lightingMode") && j["lightingMode"].is_number_integer()) {
        config.lightingMode = static_cast<Core::LightingMode>(j["lightingMode"].get<uint32_t>());
    }

    if (j.contains("restir") && j["restir"].is_object()) {
        const auto& r = j["restir"];
        auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
        auto getUint = [&](const char* k, uint32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<uint32_t>() : def; };
        auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };

        Core::ReSTIRParams& p = config.restir;
        p.bHalfRes = getBool("bHalfRes", p.bHalfRes);
        p.bSpatial2 = getBool("bSpatial2", p.bSpatial2);
        p.iblIntensity = r.contains("iblIntensity") && r["iblIntensity"].is_number() ? r["iblIntensity"].get<float>() : p.iblIntensity;
        p.mode = static_cast<Core::ReSTIRParams::Mode>(getInt("mode", static_cast<int32_t>(p.mode)));
        p.debugStop = static_cast<Core::ReSTIRDebugStop>(getInt("debugStop", static_cast<int32_t>(p.debugStop)));
        p.spatialRadius = getUint("spatialRadius", p.spatialRadius);
        p.spatialNeighbors = getUint("spatialNeighbors", p.spatialNeighbors);
        p.spatialMCap = getUint("spatialMCap", p.spatialMCap);
        p.temporalMCap = getUint("temporalMCap", p.temporalMCap);
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
    }

    if (j.contains("relax") && j["relax"].is_object()) {
        const auto& r = j["relax"];
        auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
        auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };
        auto getFloat = [&](const char* k, float def) { return r.contains(k) && r[k].is_number() ? r[k].get<float>() : def; };

        Core::RELAXParams& p = config.relax;
        p.denoisingRange = getFloat("denoisingRange", p.denoisingRange);
        p.disocclusionThreshold = getFloat("disocclusionThreshold", p.disocclusionThreshold);
        p.depthThreshold = getFloat("depthThreshold", p.depthThreshold);
        p.framerateScale = getFloat("framerateScale", p.framerateScale);
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

    if (j.contains("postProcess") && j["postProcess"].is_object()) {
        const auto& r = j["postProcess"];
        auto getBool = [&](const char* k, bool def) { return r.contains(k) && r[k].is_boolean() ? r[k].get<bool>() : def; };
        auto getInt = [&](const char* k, int32_t def) { return r.contains(k) && r[k].is_number() ? r[k].get<int32_t>() : def; };
        auto getFloat = [&](const char* k, float def) { return r.contains(k) && r[k].is_number() ? r[k].get<float>() : def; };

        config.aaMode = static_cast<Core::AntiAliasingMode>(getInt("aaMode", static_cast<int32_t>(config.aaMode)));

        Core::GTAOConfiguration& gtao = config.gtaoConfig;
        if (r.contains("gtao") && r["gtao"].is_object()) {
            const auto& g = r["gtao"];
            auto gBool = [&](const char* k, bool def) { return g.contains(k) && g[k].is_boolean() ? g[k].get<bool>() : def; };
            auto gFloat = [&](const char* k, float def) { return g.contains(k) && g[k].is_number() ? g[k].get<float>() : def; };
            gtao.bEnabled = gBool("bEnabled", gtao.bEnabled);
            gtao.effectRadius = gFloat("effectRadius", gtao.effectRadius);
            gtao.radiusMultiplier = gFloat("radiusMultiplier", gtao.radiusMultiplier);
            gtao.effectFalloffRange = gFloat("effectFalloffRange", gtao.effectFalloffRange);
            gtao.sampleDistributionPower = gFloat("sampleDistributionPower", gtao.sampleDistributionPower);
            gtao.thinOccluderCompensation = gFloat("thinOccluderCompensation", gtao.thinOccluderCompensation);
            gtao.finalValuePower = gFloat("finalValuePower", gtao.finalValuePower);
            gtao.depthMipSamplingOffset = gFloat("depthMipSamplingOffset", gtao.depthMipSamplingOffset);
            gtao.sliceCount = gFloat("sliceCount", gtao.sliceCount);
            gtao.stepsPerSlice = gFloat("stepsPerSlice", gtao.stepsPerSlice);
            gtao.denoiseBlurBeta = gFloat("denoiseBlurBeta", gtao.denoiseBlurBeta);
        }

        Core::SMAAConfiguration& smaa = config.smaaConfig;
        if (r.contains("smaa") && r["smaa"].is_object()) {
            const auto& s = r["smaa"];
            auto sInt = [&](const char* k, int32_t def) { return s.contains(k) && s[k].is_number() ? s[k].get<int32_t>() : def; };
            auto sFloat = [&](const char* k, float def) { return s.contains(k) && s[k].is_number() ? s[k].get<float>() : def; };
            smaa.edgeDetectionMode = static_cast<Core::SMAAEdgeDetectionMode>(sInt("edgeDetectionMode", static_cast<int32_t>(smaa.edgeDetectionMode)));
            smaa.threshold = sFloat("threshold", smaa.threshold);
            smaa.localContrastAdaptation = sFloat("localContrastAdaptation", smaa.localContrastAdaptation);
            smaa.maxSearchSteps = sInt("maxSearchSteps", smaa.maxSearchSteps);
            smaa.maxSearchStepsDiag = sInt("maxSearchStepsDiag", smaa.maxSearchStepsDiag);
        }

        Core::TAAConfiguration& taa = config.taaConfig;
        if (r.contains("taa") && r["taa"].is_object()) {
            const auto& t = r["taa"];
            auto tFloat = [&](const char* k, float def) { return t.contains(k) && t[k].is_number() ? t[k].get<float>() : def; };
            taa.baseBlendAlpha = tFloat("baseBlendAlpha", taa.baseBlendAlpha);
            taa.disocclusionThreshold = tFloat("disocclusionThreshold", taa.disocclusionThreshold);
            taa.varianceGammaLuma = tFloat("varianceGammaLuma", taa.varianceGammaLuma);
            taa.varianceGammaChroma = tFloat("varianceGammaChroma", taa.varianceGammaChroma);
            taa.karisStrength = tFloat("karisStrength", taa.karisStrength);
            taa.invalidHistoryBlend = tFloat("invalidHistoryBlend", taa.invalidHistoryBlend);
            taa.lumaBoostCap = tFloat("lumaBoostCap", taa.lumaBoostCap);
        }

        Core::PostProcessConfiguration& pp = config.postProcess;
        pp.bExposureEnabled = getBool("bExposureEnabled", pp.bExposureEnabled);
        pp.exposureTargetLuminance = getFloat("exposureTargetLuminance", pp.exposureTargetLuminance);
        pp.exposureAdaptationRate = getFloat("exposureAdaptationRate", pp.exposureAdaptationRate);
        pp.bBloomEnabled = getBool("bBloomEnabled", pp.bBloomEnabled);
        pp.bloomThreshold = getFloat("bloomThreshold", pp.bloomThreshold);
        pp.bloomSoftThreshold = getFloat("bloomSoftThreshold", pp.bloomSoftThreshold);
        pp.bloomRadius = getFloat("bloomRadius", pp.bloomRadius);
        pp.bloomIntensity = getFloat("bloomIntensity", pp.bloomIntensity);
        pp.bloomClamp = getFloat("bloomClamp", pp.bloomClamp);
        pp.tonemapOperator = getInt("tonemapOperator", pp.tonemapOperator);
        pp.uchimuraParams.P = getFloat("uchimuraP", pp.uchimuraParams.P);
        pp.uchimuraParams.a = getFloat("uchimuraA", pp.uchimuraParams.a);
        pp.uchimuraParams.m = getFloat("uchimuraM", pp.uchimuraParams.m);
        pp.uchimuraParams.l = getFloat("uchimuraL", pp.uchimuraParams.l);
        pp.uchimuraParams.c = getFloat("uchimuraC", pp.uchimuraParams.c);
        pp.uchimuraParams.b = getFloat("uchimuraB", pp.uchimuraParams.b);
        pp.hableParams.whitePoint = getFloat("hableWhitePoint", pp.hableParams.whitePoint);
        pp.reinhardParams.whitePoint = getFloat("reinhardWhitePoint", pp.reinhardParams.whitePoint);
        pp.agxParams.minEV = getFloat("agxMinEV", pp.agxParams.minEV);
        pp.agxParams.maxEV = getFloat("agxMaxEV", pp.agxParams.maxEV);
        pp.khronosParams.startCompression = getFloat("khronosStartCompression", pp.khronosParams.startCompression);
        pp.khronosParams.desaturation = getFloat("khronosDesaturation", pp.khronosParams.desaturation);
        pp.motionBlurVelocityScale = getFloat("motionBlurVelocityScale", pp.motionBlurVelocityScale);
        pp.motionBlurDepthScale = getFloat("motionBlurDepthScale", pp.motionBlurDepthScale);
        pp.bColorGradingEnabled = getBool("bColorGradingEnabled", pp.bColorGradingEnabled);
        pp.colorGradingExposure = getFloat("colorGradingExposure", pp.colorGradingExposure);
        pp.colorGradingContrast = getFloat("colorGradingContrast", pp.colorGradingContrast);
        pp.colorGradingSaturation = getFloat("colorGradingSaturation", pp.colorGradingSaturation);
        pp.colorGradingTemperature = getFloat("colorGradingTemperature", pp.colorGradingTemperature);
        pp.colorGradingTint = getFloat("colorGradingTint", pp.colorGradingTint);
        pp.bVignetteAberrationEnabled = getBool("bVignetteAberrationEnabled", pp.bVignetteAberrationEnabled);
        pp.chromaticAberrationStrength = getFloat("chromaticAberrationStrength", pp.chromaticAberrationStrength);
        pp.vignetteStrength = getFloat("vignetteStrength", pp.vignetteStrength);
        pp.vignetteRadius = getFloat("vignetteRadius", pp.vignetteRadius);
        pp.vignetteSmoothness = getFloat("vignetteSmoothness", pp.vignetteSmoothness);
        pp.bSharpeningEnabled = getBool("bSharpeningEnabled", pp.bSharpeningEnabled);
        pp.sharpeningStrength = getFloat("sharpeningStrength", pp.sharpeningStrength);
        pp.bPaniniEnabled = getBool("bPaniniEnabled", pp.bPaniniEnabled);
        pp.paniniStrength = getFloat("paniniStrength", pp.paniniStrength);
        pp.bFilmGrainEnabled = getBool("bFilmGrainEnabled", pp.bFilmGrainEnabled);
        pp.grainStrength = getFloat("grainStrength", pp.grainStrength);
        pp.grainSize = getFloat("grainSize", pp.grainSize);
        pp.bDitherEnabled = getBool("bDitherEnabled", pp.bDitherEnabled);
        pp.ditherStrength = getFloat("ditherStrength", pp.ditherStrength);
    }

    return config;
}

bool WriteProjectConfig(const ProjectConfig& config)
{
    const Core::Path path = GetProjectConfigPath();
    std::ofstream file(path.c_str());
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json j;
    j["defaultScene"] = std::string_view(config.defaultScene.c_str(), config.defaultScene.Size());
    j["bAutoSave"] = config.bAutoSave;
    j["lightingMode"] = config.lightingMode;

    const Core::ReSTIRParams& p = config.restir;
    j["restir"] = {
        {"bHalfRes", p.bHalfRes},
        {"bSpatial2", p.bSpatial2},
        {"iblIntensity", p.iblIntensity},
        {"mode", static_cast<int32_t>(p.mode)},
        {"debugStop", static_cast<int32_t>(p.debugStop)},
        {"spatialRadius", p.spatialRadius},
        {"spatialNeighbors", p.spatialNeighbors},
        {"spatialMCap", p.spatialMCap},
        {"temporalMCap", p.temporalMCap},
        {"denoiserMode", static_cast<int32_t>(p.denoiserMode)},
        {"remodulateOutput", static_cast<int32_t>(p.remodulateOutput)},
        {"atrous", nlohmann::json{{"iterations", p.atrous.iterations}, {"sigmaLuminance", p.atrous.sigmaLuminance}, {"sigmaNormal", p.atrous.sigmaNormal}, {"sigmaDepth", p.atrous.sigmaDepth}}},
        {"svgf", nlohmann::json{{"alphaMin", p.svgf.alphaMin}, {"gradientThreshold", p.svgf.gradientThreshold}, {"sigmaLuminance", p.svgf.sigmaLuminance}, {"sigmaNormal", p.svgf.sigmaNormal}, {"sigmaDepth", p.svgf.sigmaDepth}, {"atrousIterations", p.svgf.atrousIterations}}},
    };

    const Core::RELAXParams& rx = config.relax;
    j["relax"] = {
        {"denoisingRange", rx.denoisingRange},
        {"disocclusionThreshold", rx.disocclusionThreshold},
        {"depthThreshold", rx.depthThreshold},
        {"framerateScale", rx.framerateScale},
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

    const Core::GTAOConfiguration& gtao = config.gtaoConfig;
    const Core::SMAAConfiguration& smaa = config.smaaConfig;
    const Core::TAAConfiguration& taa = config.taaConfig;
    const Core::PostProcessConfiguration& pp = config.postProcess;
    j["postProcess"] = {
        {"aaMode", static_cast<int32_t>(config.aaMode)},
        {
            "gtao", {
                {"bEnabled", gtao.bEnabled},
                {"effectRadius", gtao.effectRadius},
                {"radiusMultiplier", gtao.radiusMultiplier},
                {"effectFalloffRange", gtao.effectFalloffRange},
                {"sampleDistributionPower", gtao.sampleDistributionPower},
                {"thinOccluderCompensation", gtao.thinOccluderCompensation},
                {"finalValuePower", gtao.finalValuePower},
                {"depthMipSamplingOffset", gtao.depthMipSamplingOffset},
                {"sliceCount", gtao.sliceCount},
                {"stepsPerSlice", gtao.stepsPerSlice},
                {"denoiseBlurBeta", gtao.denoiseBlurBeta},
            }
        },
        {
            "smaa", {
                {"edgeDetectionMode", static_cast<int32_t>(smaa.edgeDetectionMode)},
                {"threshold", smaa.threshold},
                {"localContrastAdaptation", smaa.localContrastAdaptation},
                {"maxSearchSteps", smaa.maxSearchSteps},
                {"maxSearchStepsDiag", smaa.maxSearchStepsDiag},
            }
        },
        {
            "taa", {
                {"baseBlendAlpha", taa.baseBlendAlpha},
                {"disocclusionThreshold", taa.disocclusionThreshold},
                {"varianceGammaLuma", taa.varianceGammaLuma},
                {"varianceGammaChroma", taa.varianceGammaChroma},
                {"karisStrength", taa.karisStrength},
                {"invalidHistoryBlend", taa.invalidHistoryBlend},
                {"lumaBoostCap", taa.lumaBoostCap},
            }
        },
        {"bExposureEnabled", pp.bExposureEnabled},
        {"exposureTargetLuminance", pp.exposureTargetLuminance},
        {"exposureAdaptationRate", pp.exposureAdaptationRate},
        {"bBloomEnabled", pp.bBloomEnabled},
        {"bloomThreshold", pp.bloomThreshold},
        {"bloomSoftThreshold", pp.bloomSoftThreshold},
        {"bloomRadius", pp.bloomRadius},
        {"bloomIntensity", pp.bloomIntensity},
        {"bloomClamp", pp.bloomClamp},
        {"tonemapOperator", pp.tonemapOperator},
        {"uchimuraP", pp.uchimuraParams.P},
        {"uchimuraA", pp.uchimuraParams.a},
        {"uchimuraM", pp.uchimuraParams.m},
        {"uchimuraL", pp.uchimuraParams.l},
        {"uchimuraC", pp.uchimuraParams.c},
        {"uchimuraB", pp.uchimuraParams.b},
        {"hableWhitePoint", pp.hableParams.whitePoint},
        {"reinhardWhitePoint", pp.reinhardParams.whitePoint},
        {"agxMinEV", pp.agxParams.minEV},
        {"agxMaxEV", pp.agxParams.maxEV},
        {"khronosStartCompression", pp.khronosParams.startCompression},
        {"khronosDesaturation", pp.khronosParams.desaturation},
        {"motionBlurVelocityScale", pp.motionBlurVelocityScale},
        {"motionBlurDepthScale", pp.motionBlurDepthScale},
        {"bColorGradingEnabled", pp.bColorGradingEnabled},
        {"colorGradingExposure", pp.colorGradingExposure},
        {"colorGradingContrast", pp.colorGradingContrast},
        {"colorGradingSaturation", pp.colorGradingSaturation},
        {"colorGradingTemperature", pp.colorGradingTemperature},
        {"colorGradingTint", pp.colorGradingTint},
        {"bVignetteAberrationEnabled", pp.bVignetteAberrationEnabled},
        {"chromaticAberrationStrength", pp.chromaticAberrationStrength},
        {"vignetteStrength", pp.vignetteStrength},
        {"vignetteRadius", pp.vignetteRadius},
        {"vignetteSmoothness", pp.vignetteSmoothness},
        {"bSharpeningEnabled", pp.bSharpeningEnabled},
        {"sharpeningStrength", pp.sharpeningStrength},
        {"bPaniniEnabled", pp.bPaniniEnabled},
        {"paniniStrength", pp.paniniStrength},
        {"bFilmGrainEnabled", pp.bFilmGrainEnabled},
        {"grainStrength", pp.grainStrength},
        {"grainSize", pp.grainSize},
        {"bDitherEnabled", pp.bDitherEnabled},
        {"ditherStrength", pp.ditherStrength},
    };

    file << j.dump(2);
    return file.good();
}
} // Engine
