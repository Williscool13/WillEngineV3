//
// Created by William on 2026-01-30.
//

#include "debug_components.h"

#include <json/nlohmann/json.hpp>

namespace Game::Component
{
void MotionBlurMovementComponent::Serialize(const MotionBlurMovementComponent& comp, nlohmann::json& json)
{
    json["bIsHorizontal"] = comp.bIsHorizontal;
}

void MotionBlurMovementComponent::Deserialize(MotionBlurMovementComponent& comp, const nlohmann::json& json)
{
    comp.bIsHorizontal = json["bIsHorizontal"].get<bool>();
}

void AntiGravityComponent::Serialize(const AntiGravityComponent& comp, nlohmann::json& json) {}
void AntiGravityComponent::Deserialize(AntiGravityComponent& comp, const nlohmann::json& json) {}
void FloorComponent::Serialize(const FloorComponent& comp, nlohmann::json& json) {}
void FloorComponent::Deserialize(FloorComponent& comp, const nlohmann::json& json) {}
}
