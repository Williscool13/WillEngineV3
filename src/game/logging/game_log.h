//
// Created by William on 2026-07-14.
//

#ifndef WILL_ENGINE_GAME_LOG_H
#define WILL_ENGINE_GAME_LOG_H

#include <atomic>
#include <spdlog/spdlog.h>
#include "game_log_category.h"

namespace Game
{
inline spdlog::logger* CachedGameLogger(LogCategory category)
{
    static std::atomic<spdlog::logger*> cached[static_cast<int>(LogCategory::Count)] = {};
    const int i = static_cast<int>(category);
    spdlog::logger* logger = cached[i].load(std::memory_order_relaxed);
    if (!logger) {
        logger = spdlog::get(GAME_LOGGER_CATEGORY_NAMES[i]).get();
        cached[i].store(logger, std::memory_order_relaxed);
    }
    return logger;
}
} // Game

#define WLOG_TRACE(category, fmt, ...) \
    SPDLOG_LOGGER_TRACE(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)
#define WLOG_DEBUG(category, fmt, ...) \
    SPDLOG_LOGGER_DEBUG(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)
#define WLOG_INFO(category, fmt, ...) \
    SPDLOG_LOGGER_INFO(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)
#define WLOG_WARN(category, fmt, ...) \
    SPDLOG_LOGGER_WARN(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)
#define WLOG_ERROR(category, fmt, ...) \
    SPDLOG_LOGGER_ERROR(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)
#define WLOG_CRITICAL(category, fmt, ...) \
    SPDLOG_LOGGER_CRITICAL(Game::CachedGameLogger(Game::LogCategory::category), fmt, ##__VA_ARGS__)

#endif //WILL_ENGINE_GAME_LOG_H
