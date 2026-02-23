//
// Created by William on 2026-02-23.
//

#ifndef WILL_ENGINE_LOG_CATEGORY_H
#define WILL_ENGINE_LOG_CATEGORY_H

namespace Engine
{
enum class LogCategory
{
    Engine,
    Renderer,
    Physics,
    Audio,
    Asset,
    Game,
    Temp,
    Count
};

inline constexpr const char* kCategoryNames[] = {
    "Engine", "Renderer", "Physics", "Audio", "Asset", "Game", "Temp"
};
} // Engine

#endif //WILL_ENGINE_LOG_CATEGORY_H
