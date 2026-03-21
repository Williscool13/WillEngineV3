//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "core/string_id.h"
#include "engine/resources/material/material_manager.h"
#include "engine/core/model_id.h"
#include "engine/resources/model/model_types.h"

namespace Game::Component
{
struct DirtyRenderTransformComponent
{
    // 2 to update Prev next frame.
    int32_t counter{2};
};

struct RenderTransformComponent
{
    glm::mat4 modelMatrix;
    glm::mat4 previousMatrix;
    glm::vec3 renderOffset{0.0f};
};

struct PrimitiveData
{
    uint32_t primitiveIndex;
    int32_t originalMaterialIndex;
    Engine::MaterialID materialID;
};

struct StaticMeshComponent
{
    glm::vec4 modelFlags{0.0f}; // x: visible, y: shadow-caster, zw: reserved

    std::array<PrimitiveData, 128> primitives{};
    uint8_t primitiveCount{0};

    Engine::ModelID modelId{};
    int32_t meshIndex{-1};
    std::array<Engine::MaterialID, 128> materialOverrides{};
    glm::vec3 renderOffset{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};
};

struct StaticMeshLoadingTag
{};

void RecreateStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity);

struct ProceduralMeshComponent
{
    Engine::ProceduralParams params;
    Engine::MaterialID material{};
    glm::vec4 modelFlags{0.0f};
    glm::vec3 renderOffset{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};
    PrimitiveData primitive{};
    bool bPrimitiveReady{false};
};

struct ProceduralMeshLoadingTag
{};

void RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity);

struct SplineMeshComponent
{
    std::vector<glm::vec3> controlPoints{{0,0,0},{0,0,1},{0,0,2},{0,0,3}};
    std::vector<float> controlPointRolls;
    float radius{0.5f};
    float rollAngle{0.0f};
    int32_t sides{8};
    int32_t segmentsPerSpan{8};
    bool bClosed{false};
    bool bCaps{true};
    bool bDualPath{false};
    float dualPathSpacing{1.0f};
    bool bCrossPlanks{false};
    int32_t crossPlankInterval{4};

    Engine::MaterialID material{};
    glm::vec4 modelFlags{1.0f, 1.0f, 0.0f, 0.0f};
    glm::vec3 renderOffset{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};
    PrimitiveData primitive{};
    bool bPrimitiveReady{false};
};

struct SplineMeshLoadingTag
{};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
