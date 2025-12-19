//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H

#include <filesystem>

#include "ktx.h"
#include "model_format.h"
#include "TaskScheduler.h"
#include "core/include/render_interface.h"

namespace Render
{
struct WillModel
{
public:
    WillModel();

    ~WillModel();

    WillModel(const WillModel&) = delete;

    WillModel& operator=(const WillModel&) = delete;

    WillModel(WillModel&&) noexcept = default;

    WillModel& operator=(WillModel&&) noexcept = default;

    std::filesystem::path source{};
    std::string name{};

    // Populated in asset loading thread. Used by game thread
    ModelData modelData{};

    std::vector<Core::BufferAcquireOperation> bufferAcquireOps{};
    std::vector<Core::ImageAcquireOperation> imageAcquireOps{};

    uint32_t refCount = 0;
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
