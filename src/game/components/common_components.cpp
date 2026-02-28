//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include <json/nlohmann/json.hpp>

#include "camera_components.h"
#include "physics_components.h"
#include "render_components.h"

namespace Game::Component
{
void StableIdComponent::Serialize(const StableIdComponent& comp, nlohmann::json& json)
{
    json["id"] = comp.id.id;
}

void StableIdComponent::Deserialize(StableIdComponent& comp, const nlohmann::json& json)
{
    comp.id = StringID(json["id"].get<uint64_t>());
}

void NameComponent::Serialize(const NameComponent& comp, nlohmann::json& json)
{
    json["name"] = comp.name;
}
void NameComponent::Deserialize(NameComponent& comp, const nlohmann::json& json)
{
    comp.name = json["name"].get<std::string>();
}
} // Game::Components
