//
// Created by William on 2026-01-30.
//

#include "camera_components.h"

#include <json/nlohmann/json.hpp>

namespace Game::Component
{
void FreeCameraComponent::Serialize(const FreeCameraComponent& comp, nlohmann::json& json)
{
    json["moveSpeed"] = comp.moveSpeed;
    json["lookSpeed"] = comp.lookSpeed;
    json["yaw"]       = comp.yaw;
    json["pitch"]     = comp.pitch;
}

void FreeCameraComponent::Deserialize(FreeCameraComponent& comp, const nlohmann::json& json)
{
    comp.moveSpeed = json["moveSpeed"].get<float>();
    comp.lookSpeed = json["lookSpeed"].get<float>();
    comp.yaw       = json["yaw"].get<float>();
    comp.pitch     = json["pitch"].get<float>();
}
} // Game::Component
