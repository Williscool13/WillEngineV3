//
// Created by William on 2026-01-30.
//

#include "camera_components.h"

#include <json/nlohmann/json.hpp>

void Game::Component::FreeCameraComponent::Serialize(const FreeCameraComponent& comp, nlohmann::json& json)
{
    json["moveSpeed"] = comp.moveSpeed;
    json["lookSpeed"] = comp.lookSpeed;
}

void Game::Component::FreeCameraComponent::Deserialize(FreeCameraComponent& comp, const nlohmann::json& json)
{
    comp.moveSpeed = json["moveSpeed"].get<float>();
    comp.lookSpeed = json["lookSpeed"].get<float>();
}
