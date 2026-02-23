//
// Created by William on 2026-02-23.
//

#ifndef WILL_ENGINE_ENGINE_LOG_H
#define WILL_ENGINE_ENGINE_LOG_H

#include <spdlog/spdlog.h>
#include "engine_logger.h"

#define LOG_TRACE(category, fmt, ...) \
    SPDLOG_LOGGER_TRACE(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)
#define LOG_DEBUG(category, fmt, ...) \
    SPDLOG_LOGGER_DEBUG(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)
#define LOG_INFO(category, fmt, ...) \
    SPDLOG_LOGGER_INFO(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)
#define LOG_WARN(category, fmt, ...) \
    SPDLOG_LOGGER_WARN(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)
#define LOG_ERROR(category, fmt, ...) \
    SPDLOG_LOGGER_ERROR(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(category, fmt, ...) \
    SPDLOG_LOGGER_CRITICAL(spdlog::get(Engine::kCategoryNames[(int)Engine::LogCategory::category]), fmt, ##__VA_ARGS__)

#endif //WILL_ENGINE_ENGINE_LOG_H