//
// Created by William on 2026-01-30.
//

#include "camera_components.h"

#include <json/nlohmann/json.hpp>

#include "../component-registry/component_serialization.h"

namespace Game
{
template<>
void SerializeComponent<Component::FreeCameraComponent>(const Component::FreeCameraComponent& comp, nlohmann::json& json)
{
    json["moveSpeed"] = comp.moveSpeed;
    json["lookSpeed"] = comp.lookSpeed;
}

template<>
void DeserializeComponent<Component::FreeCameraComponent>(Component::FreeCameraComponent& comp, const nlohmann::json& json)
{
    comp.moveSpeed = json["moveSpeed"].get<float>();
    comp.lookSpeed = json["lookSpeed"].get<float>();
}
}
