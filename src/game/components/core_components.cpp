//
// Created by William on 2026-02-08.
//

#include "core_components.h"

#include <json/nlohmann/json.hpp>

namespace Game::Component
{
void TransformComponent::Serialize(const TransformComponent& comp, nlohmann::json& json)
{
    json["translation"] = {comp.translation.x, comp.translation.y, comp.translation.z};
    json["rotation"]    = {comp.rotation.w, comp.rotation.x, comp.rotation.y, comp.rotation.z};
    json["scale"]       = {comp.scale.x, comp.scale.y, comp.scale.z};
}

void TransformComponent::Deserialize(TransformComponent& comp, const nlohmann::json& json)
{
    const auto& t = json["translation"];
    comp.translation = glm::vec3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());

    // glm::quat constructor order: (w, x, y, z)
    const auto& r = json["rotation"];
    comp.rotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());

    const auto& s = json["scale"];
    comp.scale = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
}
} // Game::Component