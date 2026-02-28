//
// Created by William on 2026-01-30.

#ifndef WILL_ENGINE_DEBUG_COMPONENTS_H
#define WILL_ENGINE_DEBUG_COMPONENTS_H
#include <json/nlohmann/json_fwd.hpp>

/**
 * A collection of mostly throwaway components for debugging/temporary setups.
 */
namespace Game::Component
{
struct MotionBlurMovementComponent
{
    bool bIsHorizontal{false};

    static void Serialize(const MotionBlurMovementComponent& comp, nlohmann::json& json);
    static void Deserialize(MotionBlurMovementComponent& comp, const nlohmann::json& json);
};
struct AntiGravityComponent
{
    static void Serialize(const AntiGravityComponent& comp, nlohmann::json& json);
    static void Deserialize(AntiGravityComponent& comp, const nlohmann::json& json);
};
struct FloorComponent
{
    static void Serialize(const FloorComponent& comp, nlohmann::json& json);
    static void Deserialize(FloorComponent& comp, const nlohmann::json& json);
};

struct CubemapVisualizeTag{};
}

#endif //WILL_ENGINE_DEBUG_COMPONENTS_H
