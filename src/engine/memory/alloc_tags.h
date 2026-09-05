//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_ALLOC_TAGS_H
#define WILL_ENGINE_ALLOC_TAGS_H

#include <cstdint>

namespace Core
{
enum class AllocTag : uint32_t
{
    Unknown = 0,
    // Assets
    AssetModel,
    AssetTexture,
    AssetGenerator,
    // Physics
    Physics,
    // Render
    RenderMesh,
    RenderMaterial,
    Render,
    // ECS
    ECS,
    // Scheduler
    TaskScheduler,
    // SDL
    SDL,
    // ImGui
    ImGui,
    Editor,
    // Engine systems
    EngineLogger,
    EngineContext,
    EngineState,
    GameState,
    InputManager,
    TimeManager,
    FrameSync,
    FrameSync0,
    FrameSync1,
    FrameSync2,
    FrameSync3,
    RenderThread,
    AudioManager,
    AsyncAssetLoadManager,
    AssetManager,
    MaterialManager,
    Clay,
    Meshopt,
    Vulkan,
    ParShapes,
    Earcut,
    Queue,
    Stbi,
    Bc7enc,
    HarfBuzz,
    MCPServer,

    Count
};
} // Core

#endif //WILL_ENGINE_ALLOC_TAGS_H
