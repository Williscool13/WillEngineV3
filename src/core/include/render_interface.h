//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <cstdint>
#include <string>
#include <memory>

#include <glm/glm.hpp>

#include "core/string_id.h"
#include "core/allocators/handle.h"
#include "core/math/transform.h"
#include "core/time/time_frame.h"
#include "engine/material_manager.h"
#include "glm/detail/type_quat.hpp"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"
#include "render/shaders/push_constant_interop.h"


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
    bool enabled = true;
};

struct DirectionalLight
{
    glm::vec3 direction{0.577f, -0.577f, 0.577f};
    float intensity{2.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct PostProcessConfiguration
{
    // Post Process (move into view)
    bool bEnableTemporalAntialiasing{true};

    float exposureTargetLuminance{0.18f};
    float exposureAdaptationRate{16.0f};

    float bloomThreshold{1.0f};
    float bloomSoftThreshold{0.5f};
    float bloomRadius{1.0f};
    float bloomIntensity{0.04f};

    int32_t tonemapOperator{3};

    float motionBlurVelocityScale{0.8f};
    float motionBlurDepthScale{50.0f};

    float colorGradingExposure = 0.0f;
    float colorGradingContrast = 1.0f;
    float colorGradingSaturation = 1.0f;
    float colorGradingTemperature = 0.0f;
    float colorGradingTint = 0.0f;

    float chromaticAberrationStrength{1.5f};

    float vignetteStrength{0.2f};
    float vignetteRadius{0.8f};
    float vignetteSmoothness{0.5f};

    float grainStrength{0.01f};
    float grainSize{1.5f};

    float sharpeningStrength{0.4f};
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
    uint32_t primitiveIndex;
    Engine::MaterialID materialID;
    uint32_t modelIndex;
    uint32_t gpuMaterialIndex;
    uint64_t stableId;
};

struct CustomShaderDraw
{
    StringID pipelineId;

    std::array<uint32_t, 38> pushConstantCustomData;
    std::vector<InstanceData> instances;

    int32_t stencilValue{-1}; // if >=0 will be set with dynamic state


    // Transient data read/written by the render thread during render graph construction
    uint32_t instanceBufferOffset{0};
};

struct CustomStencilDrawBatch
{
    uint32_t stencilValue{0};
    std::vector<InstanceData> instances;
};

struct DebugLine
{
    glm::vec3 start;
    glm::vec3 end;
    glm::vec4 color;
};

struct DebugBox
{
    glm::vec3 center;
    glm::vec3 extents;
    glm::quat rotation;
    glm::vec4 color;
};

struct DebugSphere
{
    glm::vec3 center;
    float radius;
    glm::vec4 color;
};

struct ViewFamily
{
    RenderView mainView{};
    std::vector<PortalView> portalViews;

    std::vector<InstanceData> mainPassInstances{256};
    std::unordered_map<std::string, CustomShaderDraw> customShaderDraws{};

    std::vector<Model> modelMatrices{256};
    std::vector<MaterialProperties> materials{256};

    int32_t skyboxIndex{-1};

    ShadowConfiguration shadowConfig{};
    DirectionalLight directionalLight{};
    // std::vector<LightInstance> allLights;

    GTAOConfiguration gtaoConfig{};
    PostProcessConfiguration postProcessConfig{};


    // Debugging
    std::string debugResourceName{};
    DebugTransformationType debugTransformationType{};
    DebugViewAspect debugViewAspect{};

    std::vector<DebugLine> debugLines;
    std::vector<DebugBox> debugBoxes;
    std::vector<DebugSphere> debugSpheres;
};

#ifndef PACKAGED_BUILD
#define DEBUG_ADD_LINE(container, ...) container.push_back(__VA_ARGS__)
#define DEBUG_ADD_BOX(container, ...) container.push_back(__VA_ARGS__)
#define DEBUG_ADD_SPHERE(container, ...) container.push_back(__VA_ARGS__)
#else
#define DEBUG_ADD_LINE(container, ...) ((void)0)
#define DEBUG_ADD_BOX(container, ...) ((void)0)
#define DEBUG_ADD_SPHERE(container, ...) ((void)0)
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

struct FrameBuffer
{
    ViewFamily mainViewFamily{};

    TimeFrame timeFrame;
    uint32_t currentFrameBuffer{};
    SwapchainRecreateCommand swapchainRecreateCommand{};
    ViewportResizeCommand viewportResizeCommand{};

    std::vector<BufferAcquireOperation> bufferAcquireOperations;
    std::vector<ImageAcquireOperation> imageAcquireOperations;

    // Debug
    bool bDrawImgui = false;
    bool bFreezeVisibility = false;
    bool bLogRDG = false;
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H
