//
// Created by William on 2026-03-26.
//

#ifndef WILL_ENGINE_PATH_MOVER_COMPONENT_H
#define WILL_ENGINE_PATH_MOVER_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/spline/spline.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"
#include "core/containers/inline_vector.h"

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

struct PathPointSettings
{
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    EasingType easing{EasingType::Linear};
    float speed{1.0f};
    float waitTime{0.0f};
};

struct PathMoverComponent
{
    Engine::Spline spline;
    Core::InlineVector<PathPointSettings, Engine::Spline::MaxPoints> pointSettings;
    PathLoopMode loopMode{PathLoopMode::PingPong};

    // Runtime state
    int32_t currentSegment{0};
    float progress{0.0f};
    int32_t direction{1};
    bool bIsWaiting{false};
    float waitTimer{0.0f};

    static void Serialize(const PathMoverComponent& comp, nlohmann::json& json);

    static void Deserialize(PathMoverComponent& comp, const nlohmann::json& json);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

float ApplyEasing(EasingType type, float t);

void EvaluatePath(const Engine::Spline& spline, const Core::InlineVector<PathPointSettings, Engine::Spline::MaxPoints>& settings, int32_t source, int32_t target, float t,
                  glm::vec3& outPos, glm::quat& outRot);
}

#endif //WILL_ENGINE_PATH_MOVER_COMPONENT_H
