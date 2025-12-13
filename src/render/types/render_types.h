//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_MODEL_TYPES_H
#define WILL_ENGINE_MODEL_TYPES_H
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "render/shaders/shader_interop.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj);

struct Meshlet
{
    glm::vec4 meshletBoundingSphere;

    glm::vec3 coneApex;
    float coneCutoff;

    glm::vec3 coneAxis;
    uint32_t vertexOffset;

    uint32_t meshletVerticesOffset;
    uint32_t meshletTriangleOffset;
    uint32_t meshletVerticesCount;
    uint32_t meshletTriangleCount;
};

struct MeshletPrimitive
{
    uint32_t meshletOffset{0};
    uint32_t meshletCount{0};
    uint32_t materialIndex{0};
    uint32_t bHasTransparent{0};
    uint32_t bHasSkinning{0};
    uint32_t padding1{0};
    uint32_t padding2{0};
    uint32_t padding3{0};
    // {3} center, {1} radius
    glm::vec4 boundingSphere{};
};

struct Instance
{
    uint32_t primitiveIndex{INT32_MAX};
    uint32_t modelIndex{INT32_MAX};
    uint32_t jointMatrixOffset{};
    uint32_t bIsAllocated{false};
};

struct Node
{
    std::string name{};
    uint32_t parent{~0u};
    uint32_t meshIndex{~0u};
    uint32_t depth{};

    uint32_t inverseBindIndex{~0u};

    glm::vec3 localTranslation{};
    glm::quat localRotation{};
    glm::vec3 localScale{};
};

struct Model
{
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 prevModelMatrix{1.0f};
    glm::vec4 flags{1.0f}; // x: visible, y: shadow-caster, zw: reserved
};

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

#endif //WILL_ENGINE_MODEL_TYPES_H