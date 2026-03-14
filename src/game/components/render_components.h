//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_RENDER_COMPONENTS_H
#define WILL_ENGINE_RENDER_COMPONENTS_H

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

#include "core/string_id.h"
#include "../../engine/resources/material/material_manager.h"
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

    StringID modelId{StringID::Invalid};
    int32_t meshIndex{-1};
    std::array<Engine::MaterialID, 128> materialOverrides{};

    // Transient
    Engine::StaticModelHandle modelHandle{};
};

struct StaticMeshLoadingTag
{};

struct ProceduralMeshComponent
{
    Engine::ProceduralParams params;
    Engine::MaterialID material{};
    glm::vec4 modelFlags{0.0f};

    // Transient
    Engine::StaticModelHandle modelHandle{};
    PrimitiveData primitive{};
    bool bPrimitiveReady{false};
};

struct ProceduralMeshLoadingTag
{};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
