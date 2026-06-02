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
    float farPlane;
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

enum class ShadowQuality
{
    Ultra = 0,
    High,
    Medium,
    Low,
    Custom
};


struct ShadowConfiguration
{
    float cascadeNearPlane = 0.1f;
    float cascadeFarPlane = 100.0f;
    float splitLambda = 0.5f;
    float splitOverlap = 1.10f;
    Render::ShadowCascadePreset cascadePreset = Render::SHADOW_PRESETS[static_cast<uint32_t>(ShadowQuality::Medium)];
    float shadowIntensity = 0.0f; // lower is darker
    bool enabled = false;
};

struct DirectionalLight
{
    glm::vec3 direction{0.577f, -0.577f, 0.577f};
    float intensity{2.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
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

struct InstanceData
{
    uint32_t primitiveIndex{};
    Engine::MaterialID materialID{};
    uint32_t modelIndex{};
    uint64_t stableId{};
};

struct CustomShaderDraw
{
    InlineString<> prefix;
    StringID pipelineId;

    Array<uint32_t, 38> pushConstantCustomData;
    /**
     * Allocated when added to custom draw map in ViewFamily
     */
    Vector<InstanceData> instances;

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

#ifndef PACKAGED_BUILD
#define DEBUG_ADD_LINE(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_BOX(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_SPHERE(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_RECT(container, ...) container.PushBack(__VA_ARGS__)
#define DEBUG_ADD_ARROW(container, ...) container.PushBack(__VA_ARGS__)
#else
#define DEBUG_ADD_LINE(container, ...) ((void)0)
#define DEBUG_ADD_BOX(container, ...) ((void)0)
#define DEBUG_ADD_SPHERE(container, ...) ((void)0)
#define DEBUG_ADD_RECT(container, ...) ((void)0)
#define DEBUG_ADD_ARROW(container, ...) ((void)0)
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
    size_t instances{128};
    size_t worldGlyphQuads{256};
    size_t textInstances{32};
    size_t modelMatrices{256};
    size_t activeMaterials{256};
    size_t materials{256};
    size_t activeTextMaterials{32};
    size_t textMaterials{256};
    size_t debugLines{256};
    size_t debugBoxes{256};
    size_t debugSpheres{256};
    size_t debugRects{256};
    size_t debugArrows{256};
    size_t uiDrawCommands{64};
    size_t uiGlyphQuads{512};
    size_t textDrawCalls{256};
    size_t sprites{256};
    size_t spriteBatches{16};
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

    ArenaVector<InstanceData> instances{};
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

    ShadowConfiguration shadowConfig{};
    DirectionalLight directionalLight{};
    InlineVector<PointLightData, MAX_POINT_LIGHTS> pointLights{};
    InlineVector<AreaLightData, MAX_AREA_LIGHTS> areaLights{};

    AntiAliasingMode aaMode{AntiAliasingMode::TAA};
    GTAOConfiguration gtaoConfig{};
    SMAAConfiguration smaaConfig{};
    PostProcessConfiguration postProcessConfig{};


    // Debugging
    InlineString<> debugResourceName{};
    DebugTransformationType debugTransformationType{};
    DebugViewAspect debugViewAspect{};

    ArenaVector<DebugLine> debugLines{};
    ArenaVector<DebugBox> debugBoxes{};
    ArenaVector<DebugSphere> debugSpheres{};
    ArenaVector<DebugRect> debugRects{};
    ArenaVector<DebugArrow> debugArrows{};

    // UI: ordered draw list mirrors Clay's command sequence for correct scissor interleaving
    ArenaVector<UIDrawCommand> uiDrawList{};
    ArenaVector<UIGlyphQuad> uiGlyphQuads{};

    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};

    // Written to on render thread
    ArenaFixedMap<StringID, uint32_t> lightingBuckets{};
    ArenaVector<TextDrawCall> textDrawCalls{};
    ArenaVector<SpriteBatch> spriteBatches{};
};

enum class ReSTIRDebugStop : uint8_t
{
    Spatial2 = 0,
    Spatial1,
    Temporal,
    Generate,
};

struct ReSTIRParams
{
    bool bGroundTruthMode{false};
    bool bResetGroundTruth{false};

    ReSTIRDebugStop debugStop{ReSTIRDebugStop::Spatial2};
    uint32_t spatialRadius{30};
    uint32_t spatialNeighbors{5};
    uint32_t spatialMCap{500};
    uint32_t temporalMCap{20u * 33u};

    enum class DenoiserMode { None = 0, ATrous = 1, ASVGF = 2 };
    DenoiserMode denoiserMode{DenoiserMode::ASVGF};

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
        float gradientThreshold{0.06f};
        float sigmaLuminance{4.0f};
        float sigmaNormal{64.0f};
        float sigmaDepth{0.05f};
        int32_t atrousIterations{4};
    };
    SVGFParams svgf{};
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
    bool bLogRDG = false;
    ReSTIRParams restir{};

    bool bTakeScreenshot{false};
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H
