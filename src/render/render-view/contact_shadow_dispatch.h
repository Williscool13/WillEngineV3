//
// Port of Bend Studio's screen space shadows (bend_sss_cpu.h).
// Copyright 2023 Sony Interactive Entertainment.
// Licensed under the Apache License, Version 2.0: http://www.apache.org/licenses/LICENSE-2.0
//

#ifndef WILL_ENGINE_CONTACT_SHADOW_DISPATCH_H
#define WILL_ENGINE_CONTACT_SHADOW_DISPATCH_H

#include <cstdint>

#include <glm/glm.hpp>

namespace Render
{
inline constexpr int32_t CONTACT_SHADOW_WAVE_SIZE = 64;

struct ContactShadowDispatch
{
    int32_t waveCount[3]{};
    glm::ivec2 waveOffset{0};
};

struct ContactShadowDispatchList
{
    glm::vec4 lightCoordinate{0.0f};
    ContactShadowDispatch dispatches[8]{};
    int32_t dispatchCount{0};
};

/**
 * Splits the screen into wavefront dispatches that march towards the light's screen position.
 * @param lightProjection viewProj * (toLight, 0) for a directional light, viewProj * (position, 1) for a point light
 */
ContactShadowDispatchList BuildContactShadowDispatchList(const glm::vec4& lightProjection, glm::ivec2 viewportSize);
} // Render

#endif //WILL_ENGINE_CONTACT_SHADOW_DISPATCH_H
