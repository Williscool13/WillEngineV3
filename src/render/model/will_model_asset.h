//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H

#include <filesystem>

#include "model_types.h"
#include "TaskScheduler.h"
#include "core/include/render_interface.h"

namespace Render
{
struct WillModel;
}

namespace Engine
{
using WillModelHandle = Core::Handle<Render::WillModel>;
using MaterialHandle = Core::Handle<MaterialProperties>;
}

namespace Render
{
struct WillModel
{
public:
    enum class ModelLoadState
    {
        NotLoaded,
        Loaded,
        FailedToLoad
    };
public:
    WillModel();

    ~WillModel();

    WillModel(const WillModel&) = delete;

    WillModel& operator=(const WillModel&) = delete;

    WillModel(WillModel&&) noexcept = default;

    WillModel& operator=(WillModel&&) noexcept = default;

    // Populated by AssetManager
    std::filesystem::path source{};
    std::string name{};
    Engine::WillModelHandle selfHandle;
    ModelLoadState modelLoadState{ModelLoadState::NotLoaded};
    uint32_t refCount = 0;

    // Populated in AssetLoadThread
    ModelData modelData{};
    std::vector<Core::BufferAcquireOperation> bufferAcquireOps{};
    std::vector<Core::ImageAcquireOperation> imageAcquireOps{};
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
