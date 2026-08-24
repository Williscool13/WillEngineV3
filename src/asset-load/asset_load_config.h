//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_ASSET_LOAD_CONFIG_H
#define WILL_ENGINE_ASSET_LOAD_CONFIG_H
#include <cstdint>

namespace AssetLoad
{
inline constexpr uint32_t MAX_ASSET_LOAD_JOB_COUNT = 64;

static constexpr uint32_t AUDIO_JOB_COUNT = 256;
static constexpr uint32_t PIPELINE_JOB_COUNT = 64;
static constexpr uint32_t MODEL_JOB_COUNT = 6;
static constexpr uint32_t PROCEDURAL_MODEL_JOB_COUNT = 6;
static constexpr uint32_t PHYSICS_COLLIDER_JOB_COUNT = 4;

inline constexpr uint32_t WILL_MODEL_JOB_COUNT = 2;
inline constexpr uint32_t WILL_MODEL_LOAD_QUEUE_COUNT = 64;
inline constexpr uint32_t TEXTURE_JOB_COUNT = 12;
inline constexpr uint32_t CUBEMAP_JOB_COUNT = 1;
inline constexpr uint32_t FONT_CURVE_JOB_COUNT = 2;
inline constexpr uint32_t PROCEDURAL_TEXTURE_JOB_COUNT = 6;
inline constexpr uint32_t PROCEDURAL_TEXTURE_DISPATCHES_PER_FRAME = 4;
inline constexpr uint32_t TEXTURE_LOAD_QUEUE_COUNT = 128;

inline constexpr uint32_t UPLOAD_STAGING_DEPOT_MAX = MODEL_JOB_COUNT + PROCEDURAL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT + CUBEMAP_JOB_COUNT + FONT_CURVE_JOB_COUNT;
inline constexpr uint64_t UPLOAD_STAGING_MIP0_DIVISOR = 1;
inline constexpr uint64_t UPLOAD_STAGING_MIN_SIZE = 1ull * 1024 * 1024;  // 1MB
inline constexpr uint64_t UPLOAD_STAGING_MAX_SIZE = 64ull * 1024 * 1024; // 64MB

inline constexpr uint64_t BLAS_SCRATCH_SLOT_SIZE = 4ull * 1024 * 1024;

// During loading screens, game/engine can increase asset loading memory budget
inline constexpr uint64_t UPLOAD_STAGING_BUDGET_DEFAULT = 128ull * 1024 * 1024;
inline constexpr uint64_t UPLOAD_STAGING_BUDGET_LOADING_SCREEN = 256ull * 1024 * 1024;

inline constexpr uint64_t UPLOAD_STAGING_IDLE_FLOOR = 32ull * 1024 * 1024;
inline constexpr uint32_t UPLOAD_STAGING_IDLE_TRIM_SECONDS = 10;

static_assert(UPLOAD_STAGING_BUDGET_DEFAULT >= UPLOAD_STAGING_MAX_SIZE, "budget must fit the largest single checkout");
static_assert(UPLOAD_STAGING_BUDGET_LOADING_SCREEN >= UPLOAD_STAGING_MAX_SIZE, "budget must fit the largest single checkout");
static_assert(UPLOAD_STAGING_MIN_SIZE <= UPLOAD_STAGING_MAX_SIZE, "staging clamp range is inverted");
static_assert(UPLOAD_STAGING_IDLE_FLOOR <= UPLOAD_STAGING_BUDGET_DEFAULT, "idle floor must sit under the gameplay budget");
}

#endif //WILL_ENGINE_ASSET_LOAD_CONFIG_H
