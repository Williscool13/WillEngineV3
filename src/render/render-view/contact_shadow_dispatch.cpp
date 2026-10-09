//
// Port of Bend Studio's screen space shadows (bend_sss_cpu.h).
// Copyright 2023 Sony Interactive Entertainment.
// Licensed under the Apache License, Version 2.0: http://www.apache.org/licenses/LICENSE-2.0
//

#include "contact_shadow_dispatch.h"

#include <algorithm>

namespace Render
{
ContactShadowDispatchList BuildContactShadowDispatchList(const glm::vec4& lightProjection, glm::ivec2 viewportSize)
{
    constexpr int32_t WAVE = CONTACT_SHADOW_WAVE_SIZE;
    ContactShadowDispatchList result{};

    // Division precision breaks down when the light is very far off screen
    float xyLightW = lightProjection.w;
    const float fpLimit = 0.000002f * static_cast<float>(WAVE);
    if (xyLightW >= 0.0f && xyLightW < fpLimit) { xyLightW = fpLimit; }
    else if (xyLightW < 0.0f && xyLightW > -fpLimit) { xyLightW = -fpLimit; }

    // Pixel rows follow ndc y here (row 0 at ndc -1), unlike D3D, so y is not negated
    result.lightCoordinate.x = (lightProjection.x / xyLightW * 0.5f + 0.5f) * static_cast<float>(viewportSize.x);
    result.lightCoordinate.y = (lightProjection.y / xyLightW * 0.5f + 0.5f) * static_cast<float>(viewportSize.y);
    result.lightCoordinate.z = lightProjection.w == 0.0f ? 0.0f : lightProjection.z / lightProjection.w;
    result.lightCoordinate.w = lightProjection.w > 0.0f ? 1.0f : -1.0f;

    const int32_t lightXY[2] = {static_cast<int32_t>(result.lightCoordinate.x + 0.5f), static_cast<int32_t>(result.lightCoordinate.y + 0.5f)};

    // Inclusive bounds relative to the light
    const int32_t biasedBounds[4] = {
        0 - lightXY[0],
        -(viewportSize.y - lightXY[1]),
        viewportSize.x - lightXY[0],
        -(0 - lightXY[1]),
    };

    // 4 quadrants with a corner on the light; a non-square quadrant splits in two on its longer axis
    for (int32_t q = 0; q < 4; q++) {
        const bool bVertical = q == 0 || q == 3;
        const int32_t bounds[4] = {
            std::max(0, (q & 1) ? biasedBounds[0] : -biasedBounds[2]) / WAVE,
            std::max(0, (q & 2) ? biasedBounds[1] : -biasedBounds[3]) / WAVE,
            std::max(0, ((q & 1) ? biasedBounds[2] : -biasedBounds[0]) + WAVE * (bVertical ? 1 : 2) - 1) / WAVE,
            std::max(0, ((q & 2) ? biasedBounds[3] : -biasedBounds[1]) + WAVE * (bVertical ? 2 : 1) - 1) / WAVE,
        };

        if (bounds[2] - bounds[0] <= 0 || bounds[3] - bounds[1] <= 0) {
            continue;
        }

        const int32_t biasX = (q == 2 || q == 3) ? 1 : 0;
        const int32_t biasY = (q == 1 || q == 3) ? 1 : 0;

        ContactShadowDispatch& disp = result.dispatches[result.dispatchCount++];
        disp.waveCount[0] = WAVE;
        disp.waveCount[1] = bounds[2] - bounds[0];
        disp.waveCount[2] = bounds[3] - bounds[1];
        disp.waveOffset.x = ((q & 1) ? bounds[0] : -bounds[2]) + biasX;
        disp.waveOffset.y = ((q & 2) ? -bounds[3] : bounds[1]) + biasY;

        // Far corner of the quadrant, where the diagonal light ray meets the bounds
        int32_t axisDelta = +biasedBounds[0] - biasedBounds[1];
        if (q == 1) { axisDelta = +biasedBounds[2] + biasedBounds[1]; }
        if (q == 2) { axisDelta = -biasedBounds[0] - biasedBounds[3]; }
        if (q == 3) { axisDelta = -biasedBounds[2] + biasedBounds[3]; }
        axisDelta = (axisDelta + WAVE - 1) / WAVE;

        if (axisDelta <= 0) {
            continue;
        }

        ContactShadowDispatch& disp2 = result.dispatches[result.dispatchCount++];
        disp2 = disp;
        if (q == 0) {
            disp2.waveCount[2] = std::min(disp.waveCount[2], axisDelta);
            disp.waveCount[2] -= disp2.waveCount[2];
            disp2.waveOffset.y = disp.waveOffset.y + disp.waveCount[2];
            disp2.waveOffset.x--;
            disp2.waveCount[1]++;
        }
        if (q == 1) {
            disp2.waveCount[1] = std::min(disp.waveCount[1], axisDelta);
            disp.waveCount[1] -= disp2.waveCount[1];
            disp2.waveOffset.x = disp.waveOffset.x + disp.waveCount[1];
            disp2.waveCount[2]++;
        }
        if (q == 2) {
            disp2.waveCount[1] = std::min(disp.waveCount[1], axisDelta);
            disp.waveCount[1] -= disp2.waveCount[1];
            disp.waveOffset.x += disp2.waveCount[1];
            disp2.waveCount[2]++;
            disp2.waveOffset.y--;
        }
        if (q == 3) {
            disp2.waveCount[2] = std::min(disp.waveCount[2], axisDelta);
            disp.waveCount[2] -= disp2.waveCount[2];
            disp.waveOffset.y += disp2.waveCount[2];
            disp2.waveCount[1]++;
        }

        if (disp2.waveCount[1] <= 0 || disp2.waveCount[2] <= 0) {
            disp2 = result.dispatches[--result.dispatchCount];
        }
        if (disp.waveCount[1] <= 0 || disp.waveCount[2] <= 0) {
            disp = result.dispatches[--result.dispatchCount];
        }
    }

    for (int32_t i = 0; i < result.dispatchCount; i++) {
        result.dispatches[i].waveOffset *= WAVE;
    }
    return result;
}
} // Render
