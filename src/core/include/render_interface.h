//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <cstdint>

#include <glm/glm.hpp>

#include "core/time/time_frame.h"
#include "render/shaders/model_interop.h"

namespace Core
{
constexpr uint32_t FRAME_BUFFER_COUNT = 3;

struct RenderView
{
    float fovRadians;
    float aspectRatio;
    float nearPlane;
    float farPlane;
    glm::vec3 cameraPos;
    glm::vec3 cameraLookAt;
    glm::vec3 cameraUp;

    // render target color
    // render target depth
};

struct ViewFamily {
    std::vector<RenderView> views;

    std::vector<Instance> instances;
    // std::vector<LightInstance> allLights;
    // std::vector<ModelInstance> modelMatrices;
    // std::vector<MaterialInstance> materials;
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
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H