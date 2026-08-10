//
// Created by William on 2026-02-23.
//

#ifndef WILL_ENGINE_ENGINE_LOG_H
#define WILL_ENGINE_ENGINE_LOG_H

#include <atomic>
#include <spdlog/spdlog.h>
#include "engine_logger.h"

namespace Engine
{
inline spdlog::logger* CachedCategoryLogger(LogCategory category)
{
    static std::atomic<spdlog::logger*> cached[static_cast<int>(LogCategory::Count)] = {};
    const int i = static_cast<int>(category);
    spdlog::logger* logger = cached[i].load(std::memory_order_relaxed);
    if (!logger) {
        logger = spdlog::get(kCategoryNames[i]).get();
        cached[i].store(logger, std::memory_order_relaxed);
    }
    return logger;
}
} // Engine

#define LOG_TRACE(category, fmt, ...) \
    SPDLOG_LOGGER_TRACE(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)
#define LOG_DEBUG(category, fmt, ...) \
    SPDLOG_LOGGER_DEBUG(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)
#define LOG_INFO(category, fmt, ...) \
    SPDLOG_LOGGER_INFO(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)
#define LOG_WARN(category, fmt, ...) \
    SPDLOG_LOGGER_WARN(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)
#define LOG_ERROR(category, fmt, ...) \
    SPDLOG_LOGGER_ERROR(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(category, fmt, ...) \
    SPDLOG_LOGGER_CRITICAL(Engine::CachedCategoryLogger(Engine::LogCategory::category), fmt, ##__VA_ARGS__)

#define LOG_FLUSH(category) \
    do { \
        auto* _logger = Engine::CachedCategoryLogger(Engine::LogCategory::category); \
        if (_logger) { _logger->flush(); } \
    } while(0)

#endif //WILL_ENGINE_ENGINE_LOG_H
