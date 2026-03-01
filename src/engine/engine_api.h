//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H

#include <vector>

#include <entt/entt.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "game/components/component_registry.h"
#include "core/include/render_interface.h"

namespace Core
{
struct TimeFrame;
struct InputFrame;
}

namespace Engine
{
struct GameState
{
    const Core::InputFrame* inputFrame{nullptr};
    const Core::TimeFrame* timeFrame{nullptr};

    entt::registry registry;
    Game::ComponentRegistry componentRegistry{};

    std::mt19937_64 rng{std::random_device{}()};

    // Physics
    float physicsDeltaTimeAccumulator = 0.0f;
    float physicsInterpolationAlpha = 0.0f;
    std::map<JPH::BodyID, entt::entity> bodyToEntity;
    bool bEnablePhysics = true;

    // Shadows
    Core::DirectionalLight directionalLight{};
    Core::ShadowQuality shadowQuality = Core::ShadowQuality::Ultra;
    Core::ShadowConfiguration shadowConfig;

    Core::GTAOConfiguration gtaoConfig{};

    // Post-Process
    Core::PostProcessConfiguration postProcess{};

    // Asset Loading
    std::unordered_map<StringID, std::vector<WillModelHandle>> sceneModelHandles;
    bool bPendingModelResolve{false};

    // Loaded models debug
    WillModelHandle portalPlaneHandle{WillModelHandle::INVALID};

    // Debug
    bool bEnablePortal{true};
    std::string debugResourceName{};
    DebugTransformationType debugTransformationType{};
    Core::DebugViewAspect debugViewAspect{};

    ImGuizmo::OPERATION currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE currentGizmoMode{ImGuizmo::WORLD};

    StringID currentSceneId{"main_scene"_sid};
    std::vector<entt::entity> selectedEntities{};
    std::unordered_map<StringID, entt::entity> stableIdToEntityMap;

};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
