//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <cstdint>

#include <glm/glm.hpp>

#include "core/allocators/handle.h"
#include "core/time/time_frame.h"
#include "engine/material_manager.h"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"


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
};

struct RenderView
{
    ViewData currentViewData;
    ViewData previousViewData;

    // render target color
    // render target depth

    uint32_t debug;
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
    Render::ShadowCascadePreset cascadePreset = Render::SHADOW_PRESETS[static_cast<uint32_t>(ShadowQuality::Ultra)];
    float shadowIntensity = 0.0f; // lower is darker
    bool enabled = true;
};

struct DirectionalLight
{
    glm::vec3 direction{0.577f, -0.577f, 0.577f};
    float intensity{2.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct ViewFamily
{
    RenderView mainView;
    std::vector<RenderView> portalViews;

    struct InstanceData
    {
        uint32_t primitiveIndex;
        Engine::MaterialID materialID;
        uint32_t modelIndex;
        uint32_t gpuMaterialIndex;
    };

    std::vector<Model> modelMatrices;
    std::vector<InstanceData> instances;
    std::vector<MaterialProperties> materials;

    ShadowConfiguration shadowConfig{};

    DirectionalLight directionalLight{};
    // std::vector<LightInstance> allLights;

    // Post Process (move into single struct and into view)
    int32_t tonemapOperator{2};
    float exposureTargetLuminance{0.18f};
    float exposureAdaptationRate{2.0f};
};

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
    uint32_t width{0};
    uint32_t height{0};
    bool bIsMinimized{false};
};

struct FrameBuffer
{
    ViewFamily mainViewFamily;

    TimeFrame timeFrame;
    uint32_t currentFrameBuffer{};
    SwapchainRecreateCommand swapchainRecreateCommand{};

    std::vector<BufferAcquireOperation> bufferAcquireOperations;
    std::vector<ImageAcquireOperation> imageAcquireOperations;

    // Debug
    bool bDrawImgui = false;
    bool bFreezeVisibility = false;
    bool bLogRDG = false;
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H
