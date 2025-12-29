//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_RESOURCES_H
#define WILL_ENGINE_FRAME_RESOURCES_H
#include <string>

#include "render/vulkan/vk_resources.h"

namespace Render
{
/**
 * Buffers designed to be created multiple times, 1 per frame in flight.
 * \n Packaged into a struct for convenience and clarity.
 */
struct FrameResources
{
    AllocatedBuffer sceneDataBuffer;

    AllocatedBuffer instanceBuffer;
    AllocatedBuffer modelBuffer;
    AllocatedBuffer jointMatrixBuffer;
    AllocatedBuffer materialBuffer;
};
} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H