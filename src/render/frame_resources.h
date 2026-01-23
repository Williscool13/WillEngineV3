//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_RESOURCES_H
#define WILL_ENGINE_FRAME_RESOURCES_H
#include <string>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct FrameResourceLimits
{
    size_t highestInstanceBuffer{64};
    size_t highestDirectInstanceBuffer{64};
    size_t highestModelBuffer{64};
    size_t highestJointMatrixBuffer{64};
    size_t highestMaterialBuffer{64};

    size_t highestDirectIndirectCommandBuffer{64};
    size_t highestPackedVisibilityBuffer{64};
    size_t highestInstanceOffsetBuffer{64};
    size_t highestCompactedInstanceBuffer{64};
};

} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H