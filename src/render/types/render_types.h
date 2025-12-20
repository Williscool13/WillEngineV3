//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_TYPES_H
#define WILL_ENGINE_RENDER_TYPES_H
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "render/shaders/common_interop.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj);

struct TaskIndirectDrawParameters
{
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
    uint32_t padding;

    // instance/primitive properties
    uint32_t modelIndex; // public uint32_t jointMatrixOffset; - they are mutually exclusive, but for simplcity maybe just have both?
    uint32_t materialIndex;
    uint32_t meshletOffset;
    uint32_t meshletCount;
};
} // Render

#endif //WILL_ENGINE_RENDER_TYPES_H