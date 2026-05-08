//
// Created by William on 2026-05-08.
//

#ifndef WILL_ENGINE_ENGINE_ASSERT_H
#define WILL_ENGINE_ENGINE_ASSERT_H

#include "engine_log.h"
#include "engine_logger.h"

#ifdef NDEBUG
    #define ENGINE_ASSERT(category, cond, fmt, ...) ((void)0)
#else
    #define ENGINE_ASSERT(category, cond, fmt, ...) \
        do { \
            if (!(cond)) { \
                LOG_CRITICAL(category, "Assert failed [" #cond "]: " fmt, ##__VA_ARGS__); \
                Engine::EngineLogger::Flush(); \
                __debugbreak(); \
            } \
        } while(0)
#endif

#endif //WILL_ENGINE_ENGINE_ASSERT_H
