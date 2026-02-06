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
    size_t highestPrimitiveRangeBuffer{64};
    size_t highestPrimitiveToPrimitiveRangeMapBuffer{64};
    size_t highestCompactedPrimitiveBuffer{64};
    size_t highestInstanceIndirectionBuffer{64};
    size_t highestIndirectCommandBuffer{64};

    size_t highestDirectInstanceBuffer{64};
    size_t highestModelBuffer{64};
    size_t highestJointMatrixBuffer{64};
    size_t highestMaterialBuffer{64};

    size_t highestDirectIndirectCommandBuffer{64};
    size_t highestPackedVisibilityBuffer{64};
    size_t highestInstanceOffsetBuffer{64};
    size_t highestCompactedInstanceBuffer{64};

#ifndef PACKAGED_BUILD
    size_t highestDebugVertexBuffer{64};
    size_t highestDebugIndexBuffer{64};
#endif
};

} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H