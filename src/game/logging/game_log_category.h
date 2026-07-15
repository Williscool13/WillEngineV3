//
// Created by William on 2026-07-14.
//

#ifndef WILL_ENGINE_GAME_LOG_CATEGORY_H
#define WILL_ENGINE_GAME_LOG_CATEGORY_H

#include "engine/logging/engine_logger.h"

namespace Game
{
enum class LogCategory
{
    Player,
    Count
};

inline constexpr const char* GAME_LOGGER_CATEGORY_NAMES[] = {
    "Player"
};

inline void RegisterLogCategories(Engine::EngineLogger* logger)
{
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i) {
        logger->RegisterGameSubCategory(GAME_LOGGER_CATEGORY_NAMES[i]);
    }
}
} // Game

#endif //WILL_ENGINE_GAME_LOG_CATEGORY_H
