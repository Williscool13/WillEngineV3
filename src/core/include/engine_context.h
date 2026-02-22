//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <memory>

#include "spdlog/logger.h"

namespace enki
{
class TaskScheduler;
}

namespace Audio
{
class AudioManager;
}

namespace Physics
{
class PhysicsSystem;
}

namespace Render
{
struct ResourceManager;
}

namespace Engine
{
class AssetManager;
}

struct ImGuiContext;

namespace Core
{
struct WindowContext
{
    uint32_t windowWidth;
    uint32_t windowHeight;

    bool bCursorHidden;
};

struct EngineContext
{
    WindowContext windowContext;
    std::shared_ptr<spdlog::logger> logger;

    // Imgui
    ImGuiContext* imguiContext;
    bool bImguiKeyboardCaptured = false;
    bool bImguiMouseCaptured = false;

    enki::TaskScheduler* scheduler;

    //Render::ResourceManager* resourceManager;
    Engine::AssetManager* assetManager;
    Audio::AudioManager* audioManager;
    Physics::PhysicsSystem* physicsSystem;



    // Global Fn
    void (*internStringFn)(uint64_t, const char*);
    const char* (*resolveStringIdFn)(uint64_t);
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
