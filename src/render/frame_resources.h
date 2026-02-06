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
    size_t highestInstanceBuffer{128};
    size_t highestPrimitiveRangeBuffer{128};

    size_t highestPrimitiveRangePrefixSumBuffer{128};
    size_t highestBlockSumsBuffer{128};

    size_t highestInstanceIndirectionBuffer{128};
    size_t highestIndirectCommandBuffer{128};

    size_t highestDirectInstanceBuffer{128};
    size_t highestModelBuffer{128};
    size_t highestMaterialBuffer{128};

    size_t highestDirectIndirectCommandBuffer{128};
    size_t highestPackedVisibilityBuffer{128};
    size_t highestInstanceOffsetBuffer{128};
    size_t highestCompactedInstanceBuffer{128};

#ifndef PACKAGED_BUILD
    size_t highestDebugVertexBuffer{128};
    size_t highestDebugIndexBuffer{128};
#endif
};

} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H