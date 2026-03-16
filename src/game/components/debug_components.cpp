//
// Created by William on 2026-01-30.
//

#include "debug_components.h"

#include <json/nlohmann/json.hpp>

#include "../component-registry/component_serialization.h"

namespace Game
{
template<>
void SerializeComponent<Component::MotionBlurMovementComponent>(const Component::MotionBlurMovementComponent& comp, nlohmann::json& json)
{
    json["bIsHorizontal"] = comp.bIsHorizontal;
}

template<>
void DeserializeComponent<Component::MotionBlurMovementComponent>(Component::MotionBlurMovementComponent& comp, const nlohmann::json& json)
{
    comp.bIsHorizontal = json["bIsHorizontal"].get<bool>();
}
}
