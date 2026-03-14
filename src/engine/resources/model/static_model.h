//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H

#include <filesystem>

#include "model_types.h"
#include "TaskScheduler.h"
#include "core/include/render_interface.h"
#include "model_types.h"


namespace Engine
{
struct StaticModel
{
public:
    enum class ModelLoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };
public:
    StaticModel();

    ~StaticModel();

    StaticModel(const StaticModel&) = delete;

    StaticModel& operator=(const StaticModel&) = delete;

    StaticModel(StaticModel&&) noexcept = default;

    StaticModel& operator=(StaticModel&&) noexcept = default;

    // Populated by AssetManager, never changed
    std::string name{};
    StaticModelHandle selfHandle;
    ModelLoadState modelLoadState{ModelLoadState::NotLoaded};
    uint64_t acquireFrame{UINT64_MAX};

    // Populated by AssetManager, Only for normal models
    std::filesystem::path source{};
    StringID modelId{};

    // Populated by AssetManager, Only for (simple) procedural models
    ProceduralParams proceduralParams{};

    // Populated by AssetManager, sometimes changed
    uint32_t refCount = 0;

    // Populated in AssetLoadThread
    StaticModelData modelData{};
    std::vector<Core::BufferAcquireOperation> bufferAcquireOps{};
    std::vector<Core::ImageAcquireOperation> imageAcquireOps{};
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
