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
    std::unordered_map<StringID, entt::entity> stableIdToEntityMap;
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
    bool bPendingModelResolve{false};

    // Loaded models debug
    WillModelHandle portalPlaneHandle{WillModelHandle::INVALID};

    // Debug
    bool bEnablePortal{true};
    std::string debugResourceName{};
    DebugTransformationType debugTransformationType{};
    Core::DebugViewAspect debugViewAspect{};

    // If true transform imguizmo will not be drawn
    bool bCustomGizmoActive{false};
    bool bCustomGizmoActivePrev{false};

    ImGuizmo::OPERATION currentGizmoOperation{ImGuizmo::TRANSLATE};
    ImGuizmo::MODE currentGizmoMode{ImGuizmo::WORLD};
    bool bUniformScaleMode{true};

    // Gizmo snapping
    bool bSnapEnabled{false};
    float snapTranslation{0.5f};
    float snapRotation{15.0f};
    float snapScale{0.1f};

    // Scene stuff
    StringID currentSceneId{"main_scene"_sid};
    std::string currentSceneName{"Main Scene"};
    std::vector<entt::entity> selectedEntities{};
    bool bWantCopyEntities{false};
    bool bWantDeleteEntities{false};

};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
