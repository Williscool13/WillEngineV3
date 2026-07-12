//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "core/string_id.h"
#include "core/containers/arena_fixed_map.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/arena_map.h"
#include "core/containers/arena_vector.h"
#include "core/containers/vector.h"
#include "core/memory/arena_suballocator.h"
#include "core/time/time_frame.h"
#include "core/types/transform.h"
#include "engine/material_manager.h"
#include "core/containers/inline_vector.h"
#include "render/shaders/lights_interop.h"
#include "render/shaders/model_interop.h"
#include "render/shaders/push_constant_interop.h"
#include "render/shaders/text_interop.h"


namespace Core
{
constexpr uint32_t FRAME_BUFFER_COUNT = 3;

struct ViewData
{
    float fovRadians;
    float aspectRatio;
    float nearPlane;
    glm::vec3 cameraPos;
    glm::vec3 cameraLookAt;
    glm::vec3 cameraForward;
    glm::vec3 cameraUp;

    glm::mat4 view;
    glm::mat4 proj;
};

enum class DebugViewAspect
{
    None,
    Depth,
    Stencil
};

struct RenderView
{
    ViewData currentViewData;
    ViewData previousViewData;

    // render target color
    // render target depth
};

struct PortalView
{
    RenderView view;

    Transform entryPortalTransform;
    glm::vec3 entryPortalNormal;
    glm::vec3 entryPortalRight;
    glm::vec3 entryPortalUp;

    Transform exitPortalTransform;
    glm::vec3 exitPortalNormal;
    glm::vec3 exitPortalRight;
    glm::vec3 exitPortalUp;
};

struct DirectionalLight
{
    glm::vec3 direction{0.577f, -0.577f, 0.577f};
    float intensity{2.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float angularRadiusDegrees{1.0f}; // sun-disk half-angle; 0 = hard shadows
    bool bEnabled{false};
};

struct Sprite
{
    glm::vec3 worldPosition{0.0f};
    float pixelSize{24.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    uint64_t stableId{0};
    uint32_t textureIndex{0};
    uint32_t samplerIndex{0};
    bool billboard{true};
};

struct SpriteBatch
{
    uint32_t offset;
    uint32_t count;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

struct PostProcessConfiguration
{
    bool bExposureEnabled{true};
    float exposureTargetLuminance{0.18f};
    float exposureAdaptationRate{16.0f};

    bool bBloomEnabled{true};
    float bloomThreshold{1.0f};
    float bloomSoftThreshold{0.5f};
    float bloomRadius{1.0f};
    float bloomIntensity{0.04f};
    float bloomClamp{10.0f};

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

    float motionBlurVelocityScale{0.8f};
    float motionBlurDepthScale{50.0f};

    bool bColorGradingEnabled{true};
    float colorGradingExposure = 0.0f;
    float colorGradingContrast = 1.0f;
    float colorGradingSaturation = 1.0f;
    float colorGradingTemperature = 0.0f;
    float colorGradingTint = 0.0f;

    bool bVignetteAberrationEnabled{true};
    float chromaticAberrationStrength{1.5f};
    float vignetteStrength{0.2f};
    float vignetteRadius{0.8f};
    float vignetteSmoothness{0.5f};

    bool bSharpeningEnabled{true};
    float sharpeningStrength{0.4f};

    bool bPaniniEnabled{false};
    float paniniStrength{0.0f};

    bool bFilmGrainEnabled{true};
    float grainStrength{0.01f};
    float grainSize{1.5f};

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
};

/**
 * 1 Unit of rendering for the renderer. Represents 1 primitive for a mesh (multiple primitives can share the same model matrix, hence the modelIndex)
 */
struct PrimitiveInstanceData
{
    uint32_t primitiveIndex{};
    Engine::MaterialID materialID{};
    uint32_t modelIndex{};
    uint64_t stableId{};
    uint64_t blasDeviceAddress{};
    uint32_t lightIndex{0xFFFFFFFFu};
    uint32_t emissiveTriLightBase{0xFFFFFFFFu};
    bool ddgiVisible{true};
};

struct CustomShaderDraw
{
    InlineString<> prefix;
    StringID pipelineId;

    Array<uint32_t, 38> pushConstantCustomData;
    /**
     * Allocated when added to custom draw map in ViewFamily
     */
    Vector<PrimitiveInstanceData> primitiveInstances;

    int32_t stencilValue{-1}; // if >=0 will be set with dynamic state

    // Transient data read/written by the render thread during render graph construction
    uint32_t instanceBufferOffset{0};
};

struct DebugLine
{
    glm::vec3 start;
    glm::vec3 end;
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

struct DebugBox
{
    glm::vec3 center;
    glm::vec3 extents;
    glm::quat rotation;
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

struct DebugSphere
{
    glm::vec3 center;
    float radius;
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

struct DebugRect
{
    glm::vec3 center;
    float halfX;
    float halfY;
    glm::vec3 axisX;
    glm::vec3 axisY;
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

struct DebugArrow
{
    glm::vec3 start;
    glm::vec3 end;
    float headSize{0.15f};
    float shaftWidth{0.03f};
    glm::vec4 color{1.0f, 1.0f, 0.0f, 1.0f};
    float width{0.03f};
};

/** Axis = local Y. `halfHeight` is the cylindrical body half-length (excludes the hemispherical caps). */
struct DebugCapsule
{
    glm::vec3 center;
    float radius;
    float halfHeight;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

/** Axis = local Y. `halfHeight` is half the total height. */
struct DebugCylinder
{
    glm::vec3 center;
    float radius;
    float halfHeight;
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    float width{0.05f};
};

#ifdef WDEBUG
#define DEBUG_ADD_LINE(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_BOX(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_SPHERE(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_RECT(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_ARROW(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_CAPSULE(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_CYLINDER(container, ...) container.PushBack(__VA_ARGS__)
#else
#define DEBUG_ADD_LINE(container, ...) ((void)0)
#define DEBUG_ADD_BOX(container, ...) ((void)0)
#define DEBUG_ADD_SPHERE(container, ...) ((void)0)
#define DEBUG_ADD_RECT(container, ...) ((void)0)
#define DEBUG_ADD_ARROW(container, ...) ((void)0)
#define DEBUG_ADD_CAPSULE(container, ...) ((void)0)
#define DEBUG_ADD_CYLINDER(container, ...) ((void)0)
#endif


struct BufferAcquireOperation
{
    uint64_t buffer;
    uint64_t srcStageMask;
    uint64_t srcAccessMask;
    uint64_t dstStageMask;
    uint64_t dstAccessMask;
    uint64_t offset;
    uint64_t size;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
};

struct ImageAcquireOperation
{
    uint64_t image;
    uint32_t aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
    uint64_t srcStageMask;
    uint64_t srcAccessMask;
    uint32_t oldLayout;
    uint64_t dstStageMask;
    uint64_t dstAccessMask;
    uint32_t newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
};

struct SwapchainRecreateCommand
{
    bool bEngineCommandsRecreate{};
    uint32_t windowWidth{0};
    uint32_t windowHeight{0};
    bool bIsMinimized{false};
};

struct ViewportResizeCommand
{
    bool bEngineCommandsResize{false};
    uint32_t offsetX{0};
    uint32_t offsetY{0};
    uint32_t sizeX{0};
    uint32_t sizeY{0};
};

struct TextDrawCall
{
    uint32_t quadOffset;
    uint32_t quadCount;
    uint32_t atlasBindlessIndex;
    uint32_t textMaterialIndex;
};

struct TextInstanceDataFull
{
    uint32_t modelIndex{0};
    float pxRange{0.0f};
    uint32_t atlasBindlessIndex{0};
    uint32_t textMaterialIndex{0};
    uint64_t stableId{0};
};

struct UITextDrawCall
{
    uint32_t quadOffset;
    uint32_t quadCount;
    uint32_t atlasBindlessIndex;
    float pxRange;
    int32_t zIndex;
};

struct UIRenderCommandImage
{
    Vec2 pxMin;
    Vec2 pxMax;
    Vec2 uvMin;
    Vec2 uvMax;
    Vec4 tintColor;
    Vec4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    uint32_t imageBindlessIndex;
    int16_t zIndex;
};

struct UIRectDrawCall
{
    float4 color;
    float4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    float2 pxMin;
    float2 pxMax;
    int32_t zIndex;
};

struct UIScissorCommand
{
    int32_t x, y;
    uint32_t width, height;
};

struct UIOverlayColorCommand
{
    float4 color;
};

struct UIBorderDrawCall
{
    float4 color;
    float4 borderWidths; // x=left, y=right, z=top, w=bottom in pixels
    float4 cornerRadius; // x=TL, y=TR, z=BL, w=BR in pixels
    float2 pxMin; // pixel-space bounds (Clay coords, y=0 at top)
    float2 pxMax;
    int32_t zIndex;
};

enum class UICommandType : uint8_t { Rect, Image, Text, Border, ScissorPush, ScissorPop, OverlayPush, OverlayPop };

struct UIDrawCommand
{
    UICommandType type;

    union
    {
        UIScissorCommand scissor;
        UIOverlayColorCommand overlay;
        UIRectDrawCall rect;
        UIRenderCommandImage image;
        UITextDrawCall text;
        UIBorderDrawCall border;
    };
};


struct ViewFamilyWatermarks
{
    size_t primitiveInstances{128};
    size_t worldGlyphQuads{256};
    size_t textInstances{32};
    size_t modelMatrices{256};
    size_t lights{256};
    size_t activeMaterials{256};
    size_t materials{256};
    size_t activeTextMaterials{32};
    size_t textMaterials{256};
    size_t debugLines{131072};
    size_t debugBoxes{256};
    size_t debugSpheres{256};
    size_t debugRects{256};
    size_t debugArrows{256};
    size_t debugCapsules{256};
    size_t debugCylinders{256};
    size_t uiDrawCommands{64};
    size_t uiGlyphQuads{512};
    size_t textDrawCalls{256};
    size_t sprites{256};
    size_t spriteBatches{16};
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
    float disocclusionThreshold{0.005f};
    float depthThreshold{0.003f};
    float framerateScale{1.f};

    // Accumulation
    float specMaxAccumFrames{32.f};
    float specMaxFastAccumFrames{4.f};
    float diffMaxAccumFrames{32.f};
    float diffMaxFastAccumFrames{4.f};
    float historyAccelerationAmount{1.f};

    // Prepass
    float diffBlurRadius{30.f};
    float specBlurRadius{50.f};
    float minHitDistanceWeight{0.f};

    // A-Trous / edge stopping
    int32_t atrousIterations{3};
    float spatialVarianceEstimationHistoryThreshold{3.f};
    float lobeAngleFraction{0.5f};
    float roughnessFraction{0.15f};
    float specLobeAngleSlack{0.15f};
    float specPhiLuminance{2.f};
    float diffPhiLuminance{2.f};
    float diffMaxLuminanceRelativeDifference{3.f};
    float specMaxLuminanceRelativeDifference{3.f};
    float luminanceEdgeStoppingRelaxation{0.5f};
    float normalEdgeStoppingRelaxation{0.3f};
    float roughnessEdgeStoppingRelaxation{0.3f};
    float specVarianceBoost{1.f};
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
    float historyWeight{1.0f};
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
    float boilingFilterStrength{0.2f};
    bool bInitialVisibility{true};
    /** Trace the sun candidate's shadow ray BEFORE it enters the fresh RIS: an occluded sun must not win
     *  and starve the pixel of signal (slow convergence at disocclusions in sun shadow). */
    bool bSunCandidateVisibility{true};
    float brdfRoughnessMax{0.3f};
    float regirWClamp{0.0f};
    float restirWClamp{20.0f};
    bool bResetReGIR{false};
    // Emissive triangle lights
    bool bEmissiveTriangleLights{true};
    float emissiveTriRangeMultiplier{8.0f};
    int32_t emissiveTriMaxPerPrimitive{1024};
    // Temporal-gradient antilag confidence (RELAX only)
    bool bEnableConfidence{false};
    float confidenceStrength{0.75f};
    float confidenceSensitivity{3.0f};
    float confidenceDarknessBias{0.01f};
    float confidenceHistoryLength{4.0f};
    uint32_t confidenceBlurRadius{2u};

    // todo: Disabled atrous and asvgf. Readd as needed
    enum class DenoiserMode { None = 0, ATrous = 1, ASVGF = 2, RELAX = 3, ReBLUR = 4 };

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
    RELAXParams reflectionRelax{};

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

    uint32_t raysPerProbe{128};
    uint32_t outerRaysPerProbe{64};
    bool bOuterLocalNEE{false};
    bool bClassification{true};
    bool bInfiniteBounce{true};
    float bounceIntensity{0.75f};
    float maxRayRadiance{20.0f};

    float hysteresis{0.5f};
    float visibilityHysteresis{0.97f};
    float irradianceGamma{5.0f};
    float irradianceThreshold{0.25f};
    float brightnessThreshold{0.10f};
    float distanceExponent{50.0f};

    bool bApplyToLighting{true};
    bool bFinalGather{false};
    bool bFinalGatherDenoise{true};
    float normalBias{0.1f};
    float viewBias{0.3f};

    bool bRelocation{true};
    float minFrontfaceDistance{0.3f};
};

struct RTReflectionConfiguration
{
    bool bEnabled{true};
    bool bDenoiserEnabled{true};
    bool bScreenSpaceLighting{true};

    float roughnessMax{0.3f};
    float intensity{1.0f};
};

struct ViewFamily
{
    ViewFamily() = default;

    explicit ViewFamily(Arena& arena, const ViewFamilyWatermarks& wm = {});

    ~ViewFamily() = default;

    ViewFamily(const ViewFamily&) = delete;

    ViewFamily& operator=(const ViewFamily&) = delete;

    ViewFamily(ViewFamily&&) = default;

    ViewFamily& operator=(ViewFamily&&) = default;

    RenderView mainView{};
    ArenaFixedVector<PortalView> portalViews{};

    ArenaVector<PrimitiveInstanceData> primitiveInstances{};
    ArenaVector<Model> modelMatrices{};
    /** Indexes into the materials vector */
    ArenaMap<Engine::MaterialID, uint32_t> activeMaterials{};
    ArenaVector<Engine::RenderMaterial> materials{};

    ArenaVector<WorldGlyphQuad> worldGlyphQuads{};
    ArenaVector<TextInstanceDataFull> textInstances{};
    /** Indexes into text materials vector */
    ArenaMap<Engine::TextMaterialID, uint32_t> activeTextMaterials{};
    ArenaVector<TextRenderMaterial> textMaterials{};

    ArenaVector<Sprite> sprites{};

    int32_t skyboxIndex{-1};
    int32_t skyboxLOD{0};

    DirectionalLight directionalLight{};
    InlineVector<PointLightData, MAX_POINT_LIGHTS> pointLights{};
    ArenaVector<LightInfo> lights{};
    ArenaMap<uint32_t, uint32_t> lightEntityToIndex{};

    uint32_t analyticLightCount{0};
    // MeshPrimitiveStore slot -> base light index of light list
    ArenaMap<uint32_t, uint32_t> triLightBaseBySlot{};

    GTAOConfiguration gtaoConfig{};
    AntiAliasingConfiguration aaConfig{};
    PostProcessConfiguration postProcessConfig{};
    SIGMAParams sigmaParams{};
    float iblIntensity{1.0f};
    float clusterZFar{500.0f};
    float resolutionScale{1.0f};


    // Debugging
    InlineString<> debugResourceName{};
    DebugTransformationType debugTransformationType{};
    DebugViewAspect debugViewAspect{};

    ArenaVector<DebugLine> debugLines{};
    ArenaVector<DebugBox> debugBoxes{};
    ArenaVector<DebugSphere> debugSpheres{};
    ArenaVector<DebugRect> debugRects{};
    ArenaVector<DebugArrow> debugArrows{};
    ArenaVector<DebugCapsule> debugCapsules{};
    ArenaVector<DebugCylinder> debugCylinders{};

    // UI: ordered draw list mirrors Clay's command sequence for correct scissor interleaving
    ArenaVector<UIDrawCommand> uiDrawList{};
    ArenaVector<UIGlyphQuad> uiGlyphQuads{};

    // Lighting
    LightingMode lightingMode{LightingMode::Default};
    GroundTruthMode groundTruthMode{GroundTruthMode::None};
    bool bResetGroundTruth{false};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};

    // Written to on render thread
    ArenaFixedMap<StringID, uint32_t> lightingBuckets{};
    ArenaVector<TextDrawCall> textDrawCalls{};
    ArenaVector<SpriteBatch> spriteBatches{};
};


struct FrameBuffer
{
    FrameBuffer() = default;

    ~FrameBuffer() = default;

    FrameBuffer(const FrameBuffer&) = delete;

    FrameBuffer& operator=(const FrameBuffer&) = delete;

    FrameBuffer(FrameBuffer&&) = delete;

    FrameBuffer& operator=(FrameBuffer&&) = delete;

    void Initialize(ArenaSuballocator& pool, AllocTag tag = AllocTag::FrameSync0);

    void Reinitialize();

    ManagedArena frameArena{};

    ViewFamilyWatermarks viewFamilyWatermarks{};
    ViewFamily mainViewFamily{};

    TimeFrame timeFrame{};
    uint32_t currentFrameBuffer{};
    Array<uint32_t, 2> currentMousePosition{};
    SwapchainRecreateCommand swapchainRecreateCommand{};
    ViewportResizeCommand viewportResizeCommand{};

    ArenaVector<BufferAcquireOperation> bufferAcquireOperations;
    ArenaVector<ImageAcquireOperation> imageAcquireOperations;

    // Readback
    uint64_t stableIdUnderCursor{0};

    // Selection
    uint64_t selectedStableId{0};

    // Debug
    bool bDrawImgui = false;
    bool bFreezeVisibility = false;
    bool bWireframe = false;
    bool bEnableShadeDispatchBucketingVisualization = false;
    bool bEnableLightingBucketingVisualization = false;
    bool bEnableGPUDebug = false;
    bool bLockGPUDebug = false;
    bool bGPUDebugTestPattern = false;
    bool bDDGIProbeDebug = false;
    bool bClusterGridDebug = false;
    bool bWorldGridDebug = false;
    int32_t worldGridDebugLevel = 0;
    bool bWorldCacheDebug = false;
    float worldCacheDebugExposure = 1.0f;
    int32_t worldCacheDebugBucket = -1;
    bool bDDGIBounceOnly = false;
    float ddgiProbeDebugExposure = 1.0f;
    int32_t ddgiProbeDebugCascade = -1;
    bool bDDGIHideInactiveProbes = false;
    // 0 = off, 1 = irradiance, 2 = tiers, 3 = hit distance, 4 = accumulation
    int32_t giGatherDebugMode = 0;
    bool bLogRDG = false;
    ReSTIRParams restir{};
    DDGIParams ddgi{};
    RTReflectionConfiguration reflection{};

    bool bTakeScreenshot{false};
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H
