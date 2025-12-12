//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <cstdint>

#include <glm/glm.hpp>

#include "imgui/imgui_threaded_rendering.h"

namespace Core
{
constexpr uint32_t FRAME_BUFFER_COUNT = 3;

struct RawCameraData
{
    glm::mat4 view{1.0f};
    glm::mat4 prevView{1.0f};

    glm::vec3 cameraWorldPos{0.0f};
    glm::vec3 prevCameraWorldPos{0.0f};

    float prevFovDegrees{75.0f};
    float fovDegrees{75.0f};
    float prevNearPlane{0.1f};
    float nearPlane{0.1f};
    float prevFarPlane{1000.0f};
    float farPlane{1000.0f};
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
};

struct ModelMatrixOperation
{
    uint32_t index{};
    glm::mat4 modelMatrix{1.0f};

    // Filled and used by render thread
    uint32_t frames{};
};

struct InstanceOperation
{
    uint32_t index{};
    uint32_t primitiveIndex{INT32_MAX};
    uint32_t modelIndex{INT32_MAX};
    uint32_t jointMatrixOffset{0};
    uint32_t bIsAllocated{false};

    // Filled and used by render thread
    uint32_t frames{};
};

struct JointMatrixOperation
{
    uint32_t index{};
    glm::mat4 jointMatrix{};

    // Filled and used by render thread
    uint32_t frames{};
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
    SwapchainRecreateCommand swapchainRecreateCommand{};

    RawCameraData rawSceneData{};
    float timeElapsed{};
    float deltaTime{};
    uint32_t currentFrameBuffer{};

    std::vector<BufferAcquireOperation> bufferAcquireOperations;
    std::vector<ImageAcquireOperation> imageAcquireOperations;

    std::vector<ModelMatrixOperation> modelMatrixOperations;
    std::vector<InstanceOperation> instanceOperations;
    std::vector<JointMatrixOperation> jointMatrixOperations;

#ifdef WILL_EDITOR
    ImDrawDataSnapshot imguiDataSnapshot;
#endif
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H