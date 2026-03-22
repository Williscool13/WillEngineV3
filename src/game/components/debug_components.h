//
// Created by William on 2026-01-30.

#ifndef WILL_ENGINE_DEBUG_COMPONENTS_H
#define WILL_ENGINE_DEBUG_COMPONENTS_H

#include <json/nlohmann/json_fwd.hpp>

namespace Game::Component
{
struct MotionBlurMovementComponent
{
    bool bIsHorizontal{false};

    static void Serialize(const MotionBlurMovementComponent& comp, nlohmann::json& json);
    static void Deserialize(MotionBlurMovementComponent& comp, const nlohmann::json& json);
};
struct AntiGravityTag
{};
struct FloorTag
{};

struct CubemapVisualizeTag{};
}

#endif //WILL_ENGINE_DEBUG_COMPONENTS_H
