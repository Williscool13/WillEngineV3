//
// Created by William on 2026-10-07.
//

#include "csm_views.h"

namespace Render
{
CSMFrame ComputeCSMFrame(const Core::CSMParams& params, const glm::vec3& anchor, const glm::vec3& sunDirection, float nearDistance)
{
    CSMFrame frame{};
    frame.cascadeCount = static_cast<uint32_t>(glm::clamp(params.cascadeCount, 1, static_cast<int32_t>(CSM_MAX_CASCADES)));

    frame.toSun = -glm::normalize(sunDirection);
    const glm::vec3 reference = glm::abs(frame.toSun.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    frame.right = glm::normalize(glm::cross(reference, frame.toSun));
    frame.up = glm::cross(frame.toSun, frame.right);

    const float resolution = static_cast<float>(glm::max(params.resolution, 1));
    const float nearSplit = glm::max(nearDistance, 1e-3f);
    const float farSplit = glm::max(params.maxDistance, nearSplit * 2.0f);
    const float lambda = glm::clamp(params.splitLambda, 0.0f, 1.0f);
    const float casterExtension = glm::max(params.casterExtension, 0.0f);

    const float anchorX = glm::dot(anchor, frame.right);
    const float anchorY = glm::dot(anchor, frame.up);
    const float anchorZ = glm::dot(anchor, frame.toSun);

    for (uint32_t i = 0; i < frame.cascadeCount; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(frame.cascadeCount);
        const float logSplit = nearSplit * glm::pow(farSplit / nearSplit, t);
        const float evenSplit = nearSplit + (farSplit - nearSplit) * t;
        const float halfExtent = i + 1 == frame.cascadeCount ? farSplit : glm::mix(evenSplit, logSplit, lambda);

        const float texel = 2.0f * halfExtent / resolution;
        const float centerX = glm::floor(anchorX / texel) * texel;
        const float centerY = glm::floor(anchorY / texel) * texel;
        const float zFar = anchorZ - halfExtent;
        const float zNear = anchorZ + halfExtent + casterExtension;
        const float depthRange = zNear - zFar;

        // Rows: x = (dot(p, right) - centerX) / halfExtent, y likewise, z = (dot(p, toSun) - zFar) / depthRange.
        glm::mat4 viewProj(0.0f);
        for (int32_t c = 0; c < 3; ++c) {
            viewProj[c][0] = frame.right[c] / halfExtent;
            viewProj[c][1] = frame.up[c] / halfExtent;
            viewProj[c][2] = frame.toSun[c] / depthRange;
        }
        viewProj[3][0] = -centerX / halfExtent;
        viewProj[3][1] = -centerY / halfExtent;
        viewProj[3][2] = -zFar / depthRange;
        viewProj[3][3] = 1.0f;

        CSMCascade& cascade = frame.cascades[i];
        cascade.viewProj = viewProj;
        cascade.center = frame.right * centerX + frame.up * centerY + frame.toSun * (0.5f * (zNear + zFar));
        cascade.halfExtent = halfExtent;
        cascade.texelWorldSize = texel;
        cascade.depthRange = depthRange;
    }
    return frame;
}
} // Render
