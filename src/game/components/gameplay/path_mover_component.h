//
// Created by William on 2026-03-26.
//

#ifndef WILL_ENGINE_PATH_MOVER_COMPONENT_H
#define WILL_ENGINE_PATH_MOVER_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "game/components/component_types.h"
#include "core/allocators/inline_vector.h"

namespace Core
{
struct ViewFamily;
}

namespace Game::Component
{
enum class EasingType : uint8_t
{
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInQuart,
    EaseOutQuart,
    EaseInOutQuart,
    EaseInExpo,
    EaseOutExpo,
    EaseInOutExpo,
    EaseInSine,
    EaseOutSine,
    EaseInOutSine,
    COUNT
};

inline const char* EasingTypeNames[] = {
    "Linear",
    "Ease In Quad",
    "Ease Out Quad",
    "Ease In/Out Quad",
    "Ease In Cubic",
    "Ease Out Cubic",
    "Ease In/Out Cubic",
    "Ease In Quart",
    "Ease Out Quart",
    "Ease In/Out Quart",
    "Ease In Expo",
    "Ease Out Expo",
    "Ease In/Out Expo",
    "Ease In Sine",
    "Ease Out Sine",
    "Ease In/Out Sine",
};

enum class PathLoopMode : uint8_t
{
    Once,
    Loop,
    PingPong,
    COUNT
};

inline const char* PathLoopModeNames[] = {
    "Once",
    "Loop",
    "Ping-Pong",
};

struct PathControlPoint
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    EasingType easing{EasingType::Linear};
    float speed{1.0f};
    float waitTime{0.0f};
};

struct PathMoverComponent
{
    static constexpr size_t MAX_CONTROL_POINTS = 8;

    Core::InlineVector<PathControlPoint, MAX_CONTROL_POINTS> controlPoints{};
    PathLoopMode loopMode{PathLoopMode::PingPong};

    // Runtime state (not serialized, well it can be serialized, but it isn't atm)
    int32_t currentSegment{0};
    float progress{0.0f};
    int32_t direction{1};

    bool bIsWaiting{false};
    float waitTimer{0.0f};


    static void Serialize(const PathMoverComponent& comp, nlohmann::json& json);

    static void Deserialize(PathMoverComponent& comp, const nlohmann::json& json);

    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

float ApplyEasing(EasingType type, float t);

void EvaluatePath(const Core::InlineVector<PathControlPoint, PathMoverComponent::MAX_CONTROL_POINTS>& points, int32_t source, int32_t target, float progress, bool bIsLoop,
                  glm::vec3& outPos, glm::quat& outRot);
}

#endif //WILL_ENGINE_PATH_MOVER_COMPONENT_H
