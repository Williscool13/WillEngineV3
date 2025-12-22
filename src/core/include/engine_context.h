//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_CONTEXT_H
#define WILL_ENGINE_ENGINE_CONTEXT_H

#include <cstdint>
#include <memory>

#include "spdlog/logger.h"

namespace Render
{
struct ResourceManager;
}

namespace Engine
{
class AssetManager;
}

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
    //Render::ResourceManager* resourceManager;
    Engine::AssetManager* assetManager;
};
} // Core

#endif //WILL_ENGINE_ENGINE_CONTEXT_H
