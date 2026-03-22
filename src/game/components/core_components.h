//
// Created by William on 2026-02-08.
//

#ifndef WILL_ENGINE_CORE_COMPONENTS_H
#define WILL_ENGINE_CORE_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>

#include <json/nlohmann/json_fwd.hpp>

#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct TransformComponent
{
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};;

    static void Serialize(const TransformComponent& comp, nlohmann::json& json);
    static void Deserialize(TransformComponent& comp, const nlohmann::json& json);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

inline glm::mat4 GetMatrix(const TransformComponent& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) * mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}

struct DirtyTransformTag{};
}


#endif //WILL_ENGINE_CORE_COMPONENTS_H
