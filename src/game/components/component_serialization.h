//
// Created by William on 2026-03-03.
//

#ifndef WILL_ENGINE_COMPONENT_SERIALIZATION_H
#define WILL_ENGINE_COMPONENT_SERIALIZATION_H
#include <json/nlohmann/json_fwd.hpp>

#include "camera_components.h"
#include "common_components.h"
#include "core_components.h"
#include "debug_components.h"
#include "physics_components.h"
#include "render_components.h"

namespace Game
{
template<typename T>
void SerializeComponent(const T& comp, nlohmann::json& json) {}

template<typename T>
void DeserializeComponent(T& comp, const nlohmann::json& json) {}

template<> void SerializeComponent<Component::TransformComponent>(const Component::TransformComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::TransformComponent>(Component::TransformComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::StableIdComponent>(const Component::StableIdComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::StableIdComponent>(Component::StableIdComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::NameComponent>(const Component::NameComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::NameComponent>(Component::NameComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::StaticMeshComponent>(const Component::StaticMeshComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::StaticMeshComponent>(Component::StaticMeshComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::ProceduralMeshComponent>(const Component::ProceduralMeshComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::FreeCameraComponent>(const Component::FreeCameraComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::FreeCameraComponent>(Component::FreeCameraComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::MotionBlurMovementComponent>(const Component::MotionBlurMovementComponent& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::MotionBlurMovementComponent>(Component::MotionBlurMovementComponent& comp, const nlohmann::json& json);

template<> void SerializeComponent<Component::PhysicsBodyDesc>(const Component::PhysicsBodyDesc& comp, nlohmann::json& json);
template<> void DeserializeComponent<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& comp, const nlohmann::json& json);

} // Game

#endif //WILL_ENGINE_COMPONENT_SERIALIZATION_H
