//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_RESOURCES_H
#define WILL_ENGINE_FRAME_RESOURCES_H
#include <string>
#include <vector>

#include "render_config.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct FrameResourceLimits
{
    size_t highestModelBuffer{128};
    size_t highestMaterialBuffer{128};

    size_t highestInstanceBuffer{1024};
    size_t highestMeshletCount{128};

    size_t highestDirectInstanceBuffer{128};
    size_t highestDirectIndirectCommandBuffer{128};

#ifndef PACKAGED_BUILD
    size_t highestDebugVertexBuffer{128};
    size_t highestDebugIndexBuffer{128};
#endif
};

} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H