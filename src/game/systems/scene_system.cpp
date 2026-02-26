//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <json/nlohmann/json.hpp>

#include "core/component_registry.h"
#include "game/components/scene_components.h"
#include "platform/paths.h"

namespace Game::System
{
Scene SaveScene(Core::ComponentRegistry& componentRegistry, entt::registry& registry, StringID sceneId)
{
    Scene outScene{};
    nlohmann::json& scene = outScene.content;

    scene["scene_id"] = sceneId.id;
    scene["entities"] = nlohmann::json::array();

    auto view = registry.view<Component::SceneSerializedTag>();
    for (auto entity : view) {
        auto& tag = view.get<Component::SceneSerializedTag>(entity);
        if (tag.sceneId != sceneId) continue;

        nlohmann::json entityJson;

        for (Core::ComponentEntry& entry : componentRegistry.registry) {
            if (entry.has(registry, entity)) {
                nlohmann::json compJson;
                entry.serialize(registry, entity, compJson);
                entityJson[std::to_string(entry.typeId.id)] = compJson;
            }
        }

        scene["entities"].push_back(entityJson);
    }

    return outScene;
}
} // Game