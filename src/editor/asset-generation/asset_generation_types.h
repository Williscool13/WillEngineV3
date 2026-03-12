//
// Created by William on 2025-12-15.
//

#ifndef WILL_ENGINE_MODEL_GENERATION_TYPES_H
#define WILL_ENGINE_MODEL_GENERATION_TYPES_H

#include <cstdint>
#include <array>

namespace Editor
{
constexpr uint32_t ASSET_GENERATOR_WORKER_COUNT = 16;
constexpr uint32_t MODEL_GENERATION_JOB_COUNT = 4;
constexpr uint32_t TEXTURE_GENERATION_JOB_COUNT = 4;
constexpr uint32_t ENVIRONMENT_MAP_GENERATION_JOB_COUNT = 1;
constexpr uint32_t TEXTURE_GENERATION_STAGING_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB
constexpr uint32_t ENVIRONMENT_MAP_GENERATION_STAGING_BUFFER_SIZE = 128 * 1024 * 1024; // 128MB

static constexpr std::array<const char*, 32> ASSET_GENERATOR_WORKER_NAMES = {
    "AssetGenerator0", "AssetGenerator1", "AssetGenerator2", "AssetGenerator3",
    "AssetGenerator4", "AssetGenerator5", "AssetGenerator6", "AssetGenerator7",
    "AssetGenerator8", "AssetGenerator9", "AssetGenerator10", "AssetGenerator11",
    "AssetGenerator12", "AssetGenerator13", "AssetGenerator14", "AssetGenerator15",
    "AssetGenerator16", "AssetGenerator17", "AssetGenerator18", "AssetGenerator19",
    "AssetGenerator20", "AssetGenerator21", "AssetGenerator22", "AssetGenerator23",
    "AssetGenerator24", "AssetGenerator25", "AssetGenerator26", "AssetGenerator27",
    "AssetGenerator28", "AssetGenerator29", "AssetGenerator30", "AssetGenerator31",
};

} // Render

#endif //WILL_ENGINE_MODEL_GENERATION_TYPES_H