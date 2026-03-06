//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <functional>
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
class EngineLogger;
class AssetManager;
}

struct ImGuiContext;

namespace Core
{
struct WindowContext
{
    uint32_t windowWidth;
    uint32_t windowHeight;

    uint32_t viewportWidth;
    uint32_t viewportHeight;
    uint32_t viewportOffsetX;
    uint32_t viewportOffsetY;
};

struct EngineContext
{
    WindowContext windowContext;

    Engine::EngineLogger* engineLogger;

    // Imgui
    ImGuiContext* imguiContext;
    bool bImguiKeyboardCaptured = false;
    bool bImguiMouseCaptured = false;
    bool bImGuiWantsTextInput = false;
    uint64_t lastKnownStableIdUnderCursor{0};

    enki::TaskScheduler* scheduler;

    //Render::ResourceManager* resourceManager;
    Engine::AssetManager* assetManager{nullptr};
    bool bModelLoadedThisFrame{false};


    Audio::AudioManager* audioManager;
    Physics::PhysicsSystem* physicsSystem;

    // Global Fn
    void (*internStringFn)(uint64_t, const char*);
    const char* (*resolveStringIdFn)(uint64_t);
    std::function<void(bool)> setCursorHiddenFn;
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
