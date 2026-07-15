//
// Created by William on 2026-07-14.
//

#ifndef WILL_ENGINE_GAME_LOG_H
#define WILL_ENGINE_GAME_LOG_H

#include <spdlog/spdlog.h>
#include "game_log_category.h"

#define WLOG_TRACE(category, fmt, ...) \
    SPDLOG_LOGGER_TRACE(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)
#define WLOG_DEBUG(category, fmt, ...) \
    SPDLOG_LOGGER_DEBUG(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)
#define WLOG_INFO(category, fmt, ...) \
    SPDLOG_LOGGER_INFO(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)
#define WLOG_WARN(category, fmt, ...) \
    SPDLOG_LOGGER_WARN(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)
#define WLOG_ERROR(category, fmt, ...) \
    SPDLOG_LOGGER_ERROR(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)
#define WLOG_CRITICAL(category, fmt, ...) \
    SPDLOG_LOGGER_CRITICAL(spdlog::get(Game::GAME_LOGGER_CATEGORY_NAMES[(int)Game::LogCategory::category]), fmt, ##__VA_ARGS__)

#endif //WILL_ENGINE_GAME_LOG_H
