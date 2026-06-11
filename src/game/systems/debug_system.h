//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_DEBUG_SYSTEM_H
#define WILL_ENGINE_DEBUG_SYSTEM_H

#include "render/interface/render_interface.h"
#include "core/input/input_frame.h"
#include "render/shaders/common_interop.h"

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
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
    {Key::NUM_2, "GBuffer Albedo", "gbuffer_two", DebugTransformationType::GBufferAlbedo, Core::DebugViewAspect::None},
    {Key::NUM_3, "GBuffer Normal", "gbuffer_one", DebugTransformationType::GBufferNormal, Core::DebugViewAspect::None},
    {Key::NUM_4, "GBuffer PBR", "gbuffer_one", DebugTransformationType::GBufferPBR, Core::DebugViewAspect::None},
    {Key::NUM_5, "GBuffer Motion Vectors", "gbuffer_one", DebugTransformationType::GBufferMotionVectors, Core::DebugViewAspect::None},
    {Key::NUM_6, "Motion Blur Tiled Max", "motion_blur_tiled_max", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_7, "Motion Blur Neighbor Max", "motion_blur_tiled_neighbor_max", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_8, "Motion Blur Output", "motion_blur_output", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_9, "Portal Deferred Resolve", "portal_deferred_resolve", DebugTransformationType::None, Core::DebugViewAspect::None},
    {Key::NUM_0, "Stencil Target", "depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil},
};

}

namespace Game
{
void DebugUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
void DebugProcessPhysicsCollisions(Engine::EngineContext* ctx, Engine::EngineState* state);
void DebugApplyGroundForces(Engine::EngineContext* ctx, Engine::EngineState* state);



#ifdef WDEBUG
void DebugRender(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
#endif
} // Game::System

#endif //WILL_ENGINE_DEBUG_SYSTEM_H
