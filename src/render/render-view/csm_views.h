//
// Created by William on 2026-10-07.
//

#ifndef WILL_ENGINE_CSM_VIEWS_H
#define WILL_ENGINE_CSM_VIEWS_H

#include <glm/glm.hpp>

#include "core/containers/array.h"
#include "render/interface/render_params.h"
#include "render/shaders/shadow_interop.h"

namespace Render
{
/** Camera-centered cascade */
struct CSMCascade
{
    glm::mat4 viewProj{1.0f};
    glm::vec3 center{0.0f};
    float halfExtent{0.0f};
    float texelWorldSize{0.0f};
    /** World distance between the sunward and far depth planes. */
    float depthRange{0.0f};
};

struct CSMFrame
{
    Core::Array<CSMCascade, CSM_MAX_CASCADES> cascades{};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 toSun{0.0f, 0.0f, 1.0f};
    uint32_t cascadeCount{0};
};

/**
 * Cascades centered on the anchor, each snapped to its texel grid.
 * @param sunDirection direction light travels
 */
CSMFrame ComputeCSMFrame(const Core::CSMParams& params, const glm::vec3& anchor, const glm::vec3& sunDirection, float nearDistance);

CSMData BuildCSMData(const CSMFrame& frame, const Core::CSMParams& params);

glm::uvec2 CSMAtlasExtent(uint32_t cascadeCount, uint32_t resolution);
} // Render

#endif //WILL_ENGINE_CSM_VIEWS_H
