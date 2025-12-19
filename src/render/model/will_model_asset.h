//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H

#include <filesystem>

#include "ktx.h"
#include "model_format.h"
#include "TaskScheduler.h"

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

    /**
     * When entry is marked as `Ready`, asset thread will not modify the contents of this struct further.
     */

    // Populated in asset loading thread. Used by game thread
    std::filesystem::path source{};
    // todo: modelData should be the final version that is used by the game (see ModelData in test bed)
    // ModelData modelData{};


    // todo: acquire ops move to the asset load thread
    // AcquireOperations modelAcquires{};

    uint32_t refCount = 0;
    // std::vector<UploadStagingHandle> uploadStagingHandles{};
    // std::chrono::steady_clock::time_point loadStartTime;
    // std::chrono::steady_clock::time_point loadEndTime;
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
