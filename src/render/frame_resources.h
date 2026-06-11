//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_RESOURCES_H
#define WILL_ENGINE_FRAME_RESOURCES_H

namespace Render
{
struct FrameResourceLimits
{
    size_t highestModelCount{128};
    size_t highestMaterialCount{128};
    size_t highestLightingCount{128};

    size_t highestInstanceCount{1024};
    size_t highestMeshletCount{128};

    size_t highestGlyphQuadCount{128};
    size_t highestUIGlyphQuadCount{128};
    size_t highestTextInstanceCount{32};
    size_t highestTextMaterialCount{32};

#ifdef WDEBUG
    size_t highestDebugSegmentCount{128};
#endif
};

} // Render

#endif //WILL_ENGINE_FRAME_RESOURCES_H