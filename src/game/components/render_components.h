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
#include "core/containers/inline_vector.h"
#include "../../engine/material_manager.h"
#include "engine/core/model_id.h"
#include "engine/asset_manager_types.h"
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

struct MeshRuntime
{
    static constexpr size_t MaxPrimitives = 128;
    Core::InlineVector<PrimitiveData, MaxPrimitives> primitives{};
    Engine::StaticModelHandle modelHandle{};

    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_RENDER_COMPONENTS_H
