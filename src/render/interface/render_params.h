//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_RENDER_PARAMS_H
#define WILL_ENGINE_RENDER_PARAMS_H

#include <cstdint>

/** Every tunable the game hands the renderer. Engine-side authoring state embeds these structs directly. */
namespace Core
{
struct PostProcessConfiguration
{
    bool bExposureEnabled{true};
    float exposureTargetLuminance{0.18f};
    float exposureSpeedBrighten{2.0f}; // 1/s, applied while adapted luminance decreases (image brightening)
    float exposureSpeedDarken{6.0f}; // 1/s, applied while adapted luminance increases (image darkening)
    float exposureMinGainEV{-6.0f}; // final exposure gain clamped to [exp2(min), exp2(max)]
    float exposureMaxGainEV{4.0f};
    float exposureLowPercentile{0.5f}; // histogram band metered, fraction of non-black pixels
    float exposureHighPercentile{0.9f};

    bool bBloomEnabled{true};
    float bloomThreshold{1.0f}; // display-relative (post-exposure) luminance
    float bloomSoftThreshold{0.5f};
    float bloomRadius{1.0f};
    float bloomIntensity{0.25f}; // per-octave weight; composite is normalized by mip count
    float bloomClamp{10.0f}; // display-relative

    int32_t tonemapOperator{10};

    struct UchimuraParams
    {
        float P{1.0f}; // max display brightness
        float a{1.0f}; // contrast
        float m{0.22f}; // linear section start
        float l{0.4f}; // linear section length
        float c{1.33f}; // black (toe power)
        float b{0.0f}; // pedestal
    } uchimuraParams;

    struct HableParams
    {
        float whitePoint{11.2f};
    } hableParams;

    struct ReinhardParams
    {
        float whitePoint{4.0f};
    } reinhardParams;

    struct AgXParams
    {
        float minEV{-12.47393f};
        float maxEV{4.026069f};
    } agxParams;

    struct KhronosParams
    {
        float startCompression{0.76f};
        float desaturation{0.15f};
    } khronosParams;

    bool bDepthOfFieldEnabled{false};
    float dofFocusDistance{5.0f}; // view units from camera to the focal plane
    float dofFocusRange{1.0f}; // fully sharp band centered on the focal plane, view units
    float dofNearTransition{2.0f}; // view units from the sharp band to the max near radius
    float dofFarTransition{20.0f}; // view units from the sharp band to the max far radius
    float dofNearRadiusPx{16.0f}; // max CoC radius in front of focus, output pixels
    float dofFarRadiusPx{16.0f}; // max CoC radius behind focus, output pixels

    bool bMotionBlurEnabled{false};
    bool bMotionBlurObjectOnly{true}; // subtract camera reprojection so only moving objects smear
    float motionBlurVelocityScale{0.8f}; // shutter fraction of inter-frame displacement
    float motionBlurTargetFps{60.0f}; // reference rate the shutter is normalized to; 0 = physical shutter (blur scales with frame time)
    float motionBlurDepthScale{1.0f}; // 1 / soft depth-classification band in view units
    float motionBlurMaxRadiusPx{32.0f}; // cap on blur reach in output pixels; neighbor-max dilation grows to match

    bool bColorGradingEnabled{true};
    float colorGradingExposure = 0.0f; // EV bias folded into exposure before tonemapping
    float colorGradingContrast = 1.0f;
    float colorGradingSaturation = 1.0f;
    float colorGradingTemperature = 0.0f;
    float colorGradingTint = 0.0f;

    bool bVignetteEnabled{true};
    float vignetteStrength{0.2f};
    float vignetteRadius{0.8f};
    float vignetteSmoothness{0.5f};
    float vignetteRoundness{0.0f}; // 0 = screen-fit ellipse, 1 = aspect-corrected circle

    bool bChromaticAberrationEnabled{true};
    float chromaticAberrationStrength{1.5f}; // pixels of R/B separation per unit aspect-corrected radius

    bool bSharpeningEnabled{true};
    float sharpeningStrength{0.4f};

    bool bPaniniEnabled{false};
    float paniniStrength{0.0f};

    bool bFilmGrainEnabled{true};
    float grainStrength{0.01f};
    float grainSize{1.5f};
    float grainResponse{1.0f}; // 0 = flat noise, 1 = film-like luminance-weighted response

    bool bDitherEnabled{true};
    float ditherStrength{1.0f};
};

enum class AntiAliasingMode
{
    None = 0,
    SMAA,
    TAA,
    SMAAT2X,
    NaiveTAA,
    DonutTAA,
};

enum class SMAAEdgeDetectionMode : int32_t
{
    Luma = 0,
    Color = 1,
    Depth = 2,
};

struct SMAAConfiguration
{
    SMAAEdgeDetectionMode edgeDetectionMode{SMAAEdgeDetectionMode::Color};
    float threshold{0.05f};
    float localContrastAdaptation{2.0f};
    int32_t maxSearchSteps{32};
    int32_t maxSearchStepsDiag{8};
};

struct TAAConfiguration
{
    float baseBlendAlpha{0.0625f};
    float disocclusionThreshold{0.02f};
    float varianceGammaLuma{1.0f};
    float varianceGammaChroma{1.0f};
    float karisStrength{1.0f};
    float invalidHistoryBlend{0.5f};
    float lumaBoostCap{0.5f};
    float grazingTurnoverStrength{30.0f};
};


struct DonutTAAConfiguration
{
    float clampingFactor{1.0f}; // mean +/- k*sigma in PQ space; < 0 disables clamping
    float newFrameWeight{0.1f}; // steady-state new-sample blend
    float maxRadiance{1000.0f}; // pqC (cd/m^2); clamped to [1e-4, 1e8] CPU-side
    bool bUseCatmullRom{true};
    bool bUseHistoryClampRelax{false}; // no mask resource wired by default
};

struct AntiAliasingConfiguration
{
    AntiAliasingMode mode{AntiAliasingMode::TAA};
    SMAAConfiguration smaa{};
    TAAConfiguration taa{};
    DonutTAAConfiguration donutTaa{};
};

struct GTAOConfiguration
{
    bool bEnabled{true};

    float effectRadius{0.5f};
    float radiusMultiplier{1.457f};
    float effectFalloffRange{0.615f};
    float sampleDistributionPower{2.0f};
    float thinOccluderCompensation{0.0f};
    float finalValuePower{2.2f};
    float depthMipSamplingOffset{3.3f};
    float sliceCount{5.0f};
    float stepsPerSlice{3.0f};
    float denoiseBlurBeta{1.2f};
    /** Edge-aware denoise passes over the raw AO, clamped to [1, 8]. Stored as float to match the slider widget. */
    float denoisePasses{2.0f};
};

enum class LightingMode : uint8_t
{
    Default = 0,
    ReSTIR, // (incl. ReGIR)
    PathTracing,
};

enum class GroundTruthMode : uint8_t
{
    None = 0,
    DI,
    GI, // DI + diffuse GI reference
    Full, // DI + diffuse GI + specular GI reference
};

enum class ReSTIRDebugStop : uint8_t
{
    Spatial1 = 0,
    Temporal,
    Generate,
};

struct RELAXParams
{
    // General
    float denoisingRange{1000.f};
    float disocclusionThreshold{0.01f};
    float depthThreshold{0.003f};
    float framerateScale{1.f};

    // Accumulation
    float specMaxAccumFrames{32.f};
    float specMaxFastAccumFrames{6.f};
    float diffMaxAccumFrames{32.f};
    float diffMaxFastAccumFrames{6.f};
    float historyAccelerationAmount{1.f};

    // Prepass
    float diffBlurRadius{30.f};
    float specBlurRadius{50.f};
    float minHitDistanceWeight{0.f};

    // A-Trous / edge stopping
    int32_t atrousIterations{5};
    // Diffuse-only CoCg iterations
    bool bChromaAtrous{true};
    int32_t chromaAtrousIterations{2};
    float chromaLumaPower{2.f};
    float spatialVarianceEstimationHistoryThreshold{3.f};
    float lobeAngleFraction{0.5f};
    float roughnessFraction{0.15f};
    float specLobeAngleSlack{0.15f};
    float specPhiLuminance{1.f};
    float diffPhiLuminance{2.f};
    float diffMaxLuminanceRelativeDifference{100.f};
    float specMaxLuminanceRelativeDifference{100.f};
    float luminanceEdgeStoppingRelaxation{1.f};
    float normalEdgeStoppingRelaxation{0.3f};
    float roughnessEdgeStoppingRelaxation{1.f};
    float specVarianceBoost{0.f};
    bool roughnessEdgeStoppingEnabled{true};

    // History fix
    float historyFixEdgeStoppingNormalPower{8.f};
    float historyFixFrameNum{4.f};
    float historyFixBasePixelStride{14.f};

    // History clamp / reset
    float fastHistoryClampingSigmaScale{2.f};
    float historyResetTemporalSigmaScale{5.f};
    float historyResetSpatialSigmaScale{1.f};
    float historyResetAmount{0.5f};

    bool enablePrepass{true};
    bool enableAntiFirefly{true};
};

struct SIGMAParams
{
    bool bHalfRes{false};
    bool enablePostBlur{true};
    float historyWeight{0.8f};
    float maxKernelPixels{32.f};
    float penumbraScale{1.f};
};

struct ReBLURParams
{
    // General
    float denoisingRange{1000.f};
    float disocclusionThreshold{0.01f};
    float disocclusionThresholdAlternate{0.05f};
    float planeDistanceSensitivity{0.02f};
    float framerateScale{1.f};
    float lobeAngleFraction{0.15f};
    float roughnessFraction{0.15f};

    // Hit distance normalization (ReblurHitDistanceParameters A/B/C/D)
    float hitDistA{3.f};
    float hitDistB{0.1f};
    float hitDistC{20.f};
    float hitDistD{-25.f};

    // Accumulation (frames; 0 stabilized disables the stabilization pass)
    float maxAccumulatedFrameNum{30.f};
    float maxFastAccumulatedFrameNum{6.f};
    float maxStabilizedFrameNum{30.f};

    // History fix / clamping
    float historyFixFrameNum{3.f};
    float historyFixBasePixelStride{14.f};
    float fastHistoryClampingSigmaScale{2.f};

    // Prepass / blur radii (pixels)
    float diffusePrepassBlurRadius{30.f};
    float specularPrepassBlurRadius{50.f};
    float minBlurRadius{1.f};
    float maxBlurRadius{30.f};
    float minHitDistanceWeight{0.1f};

    // Diffuse-only CoCg iterations
    bool bChromaAtrous{true};
    int32_t chromaAtrousIterations{2};
    float chromaLumaPower{2.f};

    // Antilag
    float antilagLuminanceSigmaScale{2.f};
    float antilagLuminanceSensitivity{3.f};

    // Stabilization / firefly suppression
    float stabilizationStrength{1.f};
    float fireflySuppressorMinRelativeScale{2.f};

    // Specular motion-vector modification thresholds (smoothstep over spec probability)
    float specProbThresholdMvLow{0.5f};
    float specProbThresholdMvHigh{0.9f};

    // Convergence (REBLUR f = 1 / (1 + k*N))
    float convergenceS{1.f};
    float convergenceB{0.2f};
    float convergenceP{0.8f};

    // Feature toggles
    int32_t hitDistanceReconstructionMode{0}; // 0 = off, 1 = AREA_3X3, 2 = AREA_5X5
    bool enablePrepass{true};
    bool enableAntiFirefly{true};
    bool enableStabilizationFireflyCleanup{false};
    bool enableTemporalStabilization{true};
};

struct ReSTIRParams
{
    uint32_t spatialPasses{1};
    bool bPermutationSampling{true};
    bool bAdaptiveSpatial{true};
    float adaptiveSpatialBoost{1.0f};
    bool bEnableAntilag{false};
    float antilagStrength{0.5f};
    uint32_t spatialRadius{30};
    uint32_t spatialNeighbors{1};
    uint32_t spatialMCap{500};
    bool bEnableTemporal{true};
    uint32_t temporalMCap{20u};
    bool bTemporalSearch{true};
    bool bCheckerboard{false};
    // With bCheckerboard: local-light reservoirs stay half-rate, sun visibility and the lighting resolve run full-rate so the denoisers see no checkerboard.
    bool bCheckerboardFullRateResolve{true};
    float boilingFilterStrength{0.2f};
    bool bInitialVisibility{true};
    bool bSunLight{true};
    float regirWClamp{0.0f};
    float restirWClamp{20.0f};
    bool bResetReGIR{false};
    // WorldGridBin = cascaded strongest-K analytic bin (default, sparse analytic scenes)
    // ReGIR = reservoir hash grid (retained for dense/emissive-triangle scenes). Only ReGIR schedules the presample/fill producer chain.
    enum class LightProposal : uint32_t { WorldGridBin = 0, ReGIR = 1 };
    LightProposal lightProposal{LightProposal::WorldGridBin};
    // Emissive triangle lights
    bool bEmissiveTriangleLights{true};
    float emissiveTriRangeMultiplier{8.0f};
    int32_t emissiveTriMaxPerPrimitive{1024};
    // Temporal-gradient antilag confidence (RELAX only)
    bool bEnableConfidence{true};
    float confidenceStrength{0.75f};
    float confidenceSensitivity{3.0f};
    float confidenceDarknessBias{0.01f};
    float confidenceHistoryLength{4.0f};
    uint32_t confidenceBlurRadius{2u};

    // todo: Disabled atrous and asvgf. Readd as needed
    enum class DenoiserMode { None = 0, ATrous = 1, ASVGF = 2, RELAX = 3, ReBLUR = 4, NRD = 5, NRDReBLUR = 6 };

    DenoiserMode denoiserMode{DenoiserMode::None};

    enum class RemodulateOutput : uint32_t { Both = 0, DiffuseOnly = 1, SpecularOnly = 2, IndirectDiffuse = 3 };

    RemodulateOutput remodulateOutput{RemodulateOutput::Both};

    struct ATrousParams
    {
        int32_t iterations{4};
        float sigmaLuminance{2.0f};
        float sigmaNormal{128.0f};
        float sigmaDepth{0.01f};
    };

    ATrousParams atrous{};

    struct SVGFParams
    {
        float alphaMin{0.1f};
        float gradientThreshold{0.0f};
        float sigmaLuminance{4.0f};
        float sigmaNormal{64.0f};
        float sigmaDepth{0.05f};
        int32_t atrousIterations{4};
    };

    SVGFParams svgf{};

    RELAXParams relax{};

    ReBLURParams reblur{};
};

struct DDGIParams
{
    bool bEnabled{true};

    // Volume (count/spacing changes restart probe history)
    int32_t probeCountX{24};
    int32_t probeCountY{12};
    int32_t probeCountZ{24};
    float probeSpacing{1.5f};
    uint32_t cascadeCount{4};
    float edgeBlendCells{3.0f};
    bool bScaleBiasPerCascade{true};
    bool bLocalVolumes{true};
    bool bDebugDrawVolumes{false};
    bool bCascadeSampling{true};
    bool bWorldVolumeGridCull{true};
    int32_t maxResidentWorldVolumes{100};
    int32_t worldVolumeWarmupBoost{8};

    uint32_t raysPerProbe{128};
    uint32_t outerRaysPerProbe{64};
    bool bClassification{true};
    bool bInfiniteBounce{true};
    float bounceIntensity{0.75f};
    float maxRayRadiance{20.0f};
    uint32_t radianceCacheShadeInterval{8};
    uint32_t radianceCacheAccumCap{16};

    float hysteresis{0.97f};
    float visibilityHysteresis{0.97f};
    float irradianceGamma{5.0f};
    float irradianceThreshold{0.25f};
    float brightnessThreshold{0.10f};
    float distanceExponent{50.0f};

    bool bApplyToLighting{true};
    bool bFinalGather{false};
    bool bFinalGatherQuarterRes{false};
    bool bFinalGatherDenoise{true};
    bool bFinalGatherChromaDenoise{true};
    uint32_t gatherChromaDenoisePasses{2};
    float gatherChromaLumaPower{2.f};
    bool bFinalGatherTemporal{true};
    bool bGatherSkipRay{false};
    uint32_t gatherRaysPerPixel{1};
    float normalBias{0.1f};
    float viewBias{0.3f};

    bool bRelocation{true};
    float minFrontfaceDistance{0.3f};
};

struct ReflectionConfiguration
{
    enum class SunMode : uint32_t { ShadowRay = 0, AlwaysLit = 1, AlwaysUnlit = 2 };

    bool bEnabled{true};
    bool bMergedDenoise{true};
    bool bScreenSpaceLighting{true};
    bool bScreenSpaceTrace{false};
    SunMode sunMode{SunMode::ShadowRay};

    float tracedRoughnessMax{0.3f};
    float lightSpecularFromReflectionsMax{0.3f};
    float mirrorRoughnessMax{0.08f};
    float intensity{1.0f};
    float maxRayIntensity{0.0f};
    float ssrThickness{0.3f};
    int32_t ssrMaxSteps{64};
};

struct ReflectionProbeConfiguration
{
    bool bEnabled{true};
    float intensity{1.0f};
    bool bDebugDraw{false};
    float bakedDiffuseClampK{4.0f};
    bool bBruteForcePick{false};
};

/** The bFreeze* members are per-stage selections; the game gates them by its master freeze during the frame copy. */
struct DebugRenderParams
{
    bool bWireframe{false};
    bool bEnableShadeDispatchBucketingVisualization{false};
    bool bEnableLightingBucketingVisualization{false};
    bool bEnableGPUDebug{false};
    bool bLockGPUDebug{false};
    bool bDDGIProbeDebug{false};
    bool bClusterGridDebug{false};
    bool bWorldGridDebug{false};
    int32_t worldGridDebugLevel{0};
    bool bRadianceCacheDebug{false};
    float radianceCacheDebugExposure{1.0f};
    int32_t radianceCacheDebugBucket{-1};
    bool bDDGIBounceOnly{false};
    bool bFreezeGIField{true};
    bool bFreezeScreenFeedback{true};
    bool bFreezeGatherRay{false};
    float ddgiProbeDebugExposure{1.0f};
    int32_t ddgiProbeDebugCascade{-1};
    bool bDDGIHideInactiveProbes{false};
    // 0 = irradiance, 1 = visibility (mean/std lobes)
    int32_t ddgiProbeDebugMode{0};
    // 0 = off, 1 = irradiance, 2 = tiers, 3 = hit distance, 4 = accumulation, 5 = escape, 6 = variance guide
    int32_t giGatherDebugMode{0};
    // 0 = off, 1 = cache cell id, 2 = cache radiance, 3 = ddgi cheb gate, 4 = ddgi mean vs dist, 5 = ddgi coverage, 6 = ddgi irradiance
    int32_t giDeconstructMode{0};
    // -1 = off, otherwise pyramid mip shown in hiz_debug_target
    int32_t hizDebugMip{-1};
    bool bOcclusionCulling{true};
    bool bOcclusionFreeze{false};
    bool bCullInstanceFrustum{true};
    bool bCullInstanceContribution{true};
    bool bCullMeshletFrustum{true};
    bool bCullMeshletCone{true};
    bool bCullMeshletContribution{true};
};
} // Core

#endif //WILL_ENGINE_RENDER_PARAMS_H
