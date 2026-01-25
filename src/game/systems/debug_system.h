//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_DEBUG_SYSTEM_H
#define WILL_ENGINE_DEBUG_SYSTEM_H
#include <cstdint>
#include <vector>

#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/material_manager.h"
#include "render/shaders/common_interop.h"

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
struct DebugHotkey
{
    Key key;
    const char* name;
    const char* resourceName;
    DebugTransformationType transform;
    Core::DebugViewAspect aspect;
};

static const DebugHotkey DEBUG_HOTKEYS[] = {
    {Key::NUM_1, "Depth Target", "depth_target", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth},
    {Key::NUM_2, "Albedo Target", "albedo_target", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_3, "Normal Target", "normal_target", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_4, "PBR Target", "pbr_target", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_5, "Velocity Target", "velocity_target", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_6, "Motion Blur Tiled Max", "motion_blur_tiled_max", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_7, "Motion Blur Neighbor Max", "motion_blur_tiled_neighbor_max", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_8, "Motion Blur Output", "motion_blur_output", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_9, "Portal Deferred Resolve", "portal_deferred_resolve", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_0, "Stencil Target", "depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil},
};

}

namespace Game::System
{
void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state);
void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state);
void DebugVisualizeCascadeCorners(Core::EngineContext* ctx, Engine::GameState* state);
} // Game::System

#endif //WILL_ENGINE_DEBUG_SYSTEM_H
