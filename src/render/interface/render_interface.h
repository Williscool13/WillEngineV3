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
#include "render/interface/render_params.h"
#include "render/shaders/reflection_probe_interop.h"
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

enum class ScreenFadeMode : uint8_t
{
    None = 0,
    Fade,
    Iris,
    Wipe,
    Dissolve,
    Letterbox,
};

/** Gameplay-driven screen cover, not a graphics setting. */
struct ScreenFadeState
{
    ScreenFadeMode mode{ScreenFadeMode::None};
    /** 0 = scene visible, 1 = fully covered. */
    float progress{0.0f};
    float softness{0.03f};
    glm::vec2 center{0.5f, 0.5f};
    glm::vec2 direction{1.0f, 0.0f};
    glm::vec3 color{0.0f, 0.0f, 0.0f};
    /** Composite after the game UI instead of before it, so the fade covers the HUD. */
    bool bDrawOverUI{false};
};

/**
 * 1 Unit of rendering for the renderer. Represents 1 primitive for a mesh (multiple primitives can share the same model matrix, hence the modelIndex).
 * Slot-indexed by stable InstanceStore index; a default-constructed entry is a dead slot.
 */
struct PrimitiveInstanceData
{
    uint32_t primitiveIndex{DEAD_SLOT_PRIMITIVE_INDEX};
    Engine::MaterialID materialID{};
    uint32_t materialIndex{0};
    uint32_t modelIndex{};
    uint64_t stableId{};
    uint64_t blasDeviceAddress{};
    uint32_t lightIndex{0xFFFFFFFFu};
    uint32_t emissiveTriLightBase{0xFFFFFFFFu};
    bool ddgiVisible{true};
    bool noMotionBlur{false};
};

struct ActiveMaterial
{
    uint32_t stableIndex{};
    Engine::RenderMaterial material{};
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
    uint32_t fontCurveByteOffset;
    uint32_t textMaterialIndex;
    uint32_t fontAtlasIndex;
    uint32_t sdfGridDims;
};

struct TextInstanceDataFull
{
    uint32_t modelIndex{0};
    uint32_t fontCurveByteOffset{0};
    uint32_t textMaterialIndex{0};
    uint32_t fontAtlasIndex{0};
    uint32_t sdfGridDims{0};
    uint64_t stableId{0};
};

struct UITextDrawCall
{
    uint32_t quadOffset;
    uint32_t quadCount;
    uint32_t fontCurveByteOffset;
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

/** One editor preview sphere, drawn cubemap-shaded at a reflection probe's capture position. */
struct ProbePreviewSphere
{
    uint32_t cubemapIndex{0};
    glm::vec3 position{0.0f};
};

/** Shared settings for the probe preview spheres. */
struct ProbePreviewSettings
{
    bool bActive{false};
    bool bIrradiance{false};
    float roughness{0.0f};
    float radius{0.5f};
};

inline constexpr size_t MAX_LOCAL_DDGI_VOLUMES = 64;
inline constexpr int32_t LOCAL_DDGI_PROBES_PER_AXIS = 10;

struct LocalDDGIVolume
{
    glm::vec3 corner{0.0f};
    float probeSpacing{0.5f};
    uint64_t volumeId{0};
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
    ArenaVector<ActiveMaterial> activeMaterials{};
    uint32_t materialWatermark{0};

    ArenaVector<WorldGlyphQuad> worldGlyphQuads{};
    ArenaVector<TextInstanceDataFull> textInstances{};
    /** Indexes into text materials vector */
    ArenaMap<Engine::TextMaterialID, uint32_t> activeTextMaterials{};
    ArenaVector<TextRenderMaterial> textMaterials{};

    ArenaVector<Sprite> sprites{};

    int32_t skyboxIndex{-1};
    int32_t skyboxLOD{0};

    DirectionalLight directionalLight{};
    ArenaVector<LightInfo> lights{};
    ArenaFixedVector<ReflectionProbeGPU> reflectionProbes{};
    ProbePreviewSettings probePreviewSettings{};
    ArenaFixedVector<ProbePreviewSphere> probePreviews{};
    ArenaFixedVector<LocalDDGIVolume> localDDGIVolumes{};

    uint32_t analyticLightCount{0};
    ArenaFixedVector<EmissiveGroup> emissiveGroups{};
    ArenaVector<uint32_t> triLightBaseBySlot{};

    GTAOConfiguration gtaoConfig{};
    AntiAliasingConfiguration aaConfig{};
    PostProcessConfiguration postProcessConfig{};
    ScreenFadeState screenFade{};
    SIGMAParams sigmaParams{};
    float iblIntensity{1.0f};
    float indirectIntensity{1.0f};
    float bakedDiffuseClampK{4.0f};
    bool bReflectionProbeBruteForce{false};
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
    uint32_t groundTruthSpp{1};
    float groundTruthDofAperture{0.0f};
    StringID shadingShaderOverride{};
    StringID lightingShaderOverride{};

    /** Written by the render thread, not the game; here only to share the frame arena. */
    ArenaFixedMap<StringID, uint32_t> lightingBuckets{};
    ArenaVector<TextDrawCall> textDrawCalls{};
    ArenaVector<SpriteBatch> spriteBatches{};
};


enum class RenderCacheReset : uint8_t
{
    None = 0,
    ScreenHistory,
    All,
};

struct FrameBuffer
{
    FrameBuffer() = default;

    ~FrameBuffer() = default;

    FrameBuffer(const FrameBuffer&) = delete;

    FrameBuffer& operator=(const FrameBuffer&) = delete;

    FrameBuffer(FrameBuffer&&) = delete;

    FrameBuffer& operator=(FrameBuffer&&) = delete;

    void Initialize(ArenaSuballocator& pool, AllocTag tag, const char* name);

    void Reinitialize();

    ManagedArena frameArena{};

    bool bArenaPeakWarned{false};

    ViewFamilyWatermarks viewFamilyWatermarks{};
    ViewFamily mainViewFamily{};

    TimeFrame timeFrame{};
    uint32_t currentFrameBuffer{};
    Array<uint32_t, 2> currentMousePosition{};
    SwapchainRecreateCommand swapchainRecreateCommand{};
    ViewportResizeCommand viewportResizeCommand{};

    ArenaVector<BufferAcquireOperation> bufferAcquireOperations;
    ArenaVector<ImageAcquireOperation> imageAcquireOperations;

    /** The one member that flows render -> game; read FRAMES_IN_FLIGHT frames later, so always stale. */
    uint64_t stableIdUnderCursor{0};

    /** Drives the selection outline pass. */
    uint64_t selectedStableId{0};

    // Written by WillEngine, before the game's frame prepare runs
    bool bDrawImgui = false;
    bool bLogRDG = false;

    DebugRenderParams debug{};

    ReSTIRParams restir{};
    DDGIParams ddgi{};
    ReflectionConfiguration reflection{};
    ReflectionProbeConfiguration reflectionProbe{};

    bool bTakeScreenshot{false};
    InlineString<512> screenshotPath{};
    bool bCaptureProbeFace{false};

    uint32_t probeCaptureCropSize{0};
    RenderCacheReset cacheReset = RenderCacheReset::None;
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H
