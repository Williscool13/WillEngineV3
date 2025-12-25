//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_CONFIG_H
#define WILL_ENGINE_PHYSICS_CONFIG_H
#include <cstdint>

namespace Physics
{
inline constexpr float PHYSICS_TIMESTEP = 1 / 60.0f;

inline constexpr uint32_t PHYSICS_TEMP_ALLOCATOR_SIZE = 32 * 1024 * 1024; // 32 MB

inline constexpr int32_t MAX_PHYSICS_JOBS = 2048;
inline constexpr uint64_t TASK_BUFFER = 64;
inline constexpr int32_t MAX_PHYSICS_TASKS = 2048 + TASK_BUFFER;

// todo: these numbers are likely too high
inline constexpr uint32_t MAX_PHYSICS_BODIES = 65536;
inline constexpr uint32_t PHYSICS_BODY_MUTEX_COUNT = 0;
inline constexpr uint32_t MAX_BODY_PAIRS = 65536;
inline constexpr uint32_t MAX_CONTACT_CONSTRAINTS = 1024 * 1 << 5;

inline constexpr uint32_t MAX_BODY_ACTIVATION_EVENTS = 256;
static constexpr uint32_t MAX_COLLISION_EVENTS = 1024;
} // Physics

#endif //WILL_ENGINE_PHYSICS_CONFIG_H