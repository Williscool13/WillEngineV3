//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_INTERFACE_H
#define WILL_ENGINE_RENDER_INTERFACE_H

#include <cstdint>

#include <glm/glm.hpp>

namespace Core
{
constexpr uint32_t FRAME_BUFFER_COUNT = 3;

struct RawCameraData
{
    glm::vec3 cameraWorldPos{0.0f};
    glm::vec3 cameraLook{0.0f};
    glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    float fovDegrees{75.0f};
    float aspectRatio{16.0f/9.0f};
    float nearPlane{0.1f};
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

    RawCameraData rawCameraData{};
    float timeElapsed{};
    float deltaTime{};
    uint32_t currentFrameBuffer{};

    std::vector<BufferAcquireOperation> bufferAcquireOperations;
    std::vector<ImageAcquireOperation> imageAcquireOperations;

    std::vector<ModelMatrixOperation> modelMatrixOperations;
    std::vector<InstanceOperation> instanceOperations;
    std::vector<JointMatrixOperation> jointMatrixOperations;
};
} // Core

#endif //WILL_ENGINE_RENDER_INTERFACE_H