//
// Created by William on 2026-01-30.
//

#include "debug_components.h"

#include <json/nlohmann/json.hpp>

void Game::Component::MotionBlurMovementComponent::Serialize(const MotionBlurMovementComponent& comp, nlohmann::json& json)
{
    json["bIsHorizontal"] = comp.bIsHorizontal;
}

void Game::Component::MotionBlurMovementComponent::Deserialize(MotionBlurMovementComponent& comp, const nlohmann::json& json)
{
    comp.bIsHorizontal = json["bIsHorizontal"].get<bool>();
}
