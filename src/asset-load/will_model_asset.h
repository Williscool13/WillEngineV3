//
// Created by William on 2025-12-18.
//

#ifndef WILL_ENGINE_WILL_MODEL_ASSET_H
#define WILL_ENGINE_WILL_MODEL_ASSET_H
#include <atomic>
#include <filesystem>

#include "i_loadable_asset.h"
#include "ktx.h"
#include "render/model/model_format.h"

namespace AssetLoad
{
class WillModelAsset : public ILoadableAsset
{
public:
    WillModelAsset();

    ~WillModelAsset();

    void TaskExecute() override;

    void ThreadExecute() override;

private:
    enum class UploadState {
        NotStarted,
        Uploading,
        UploadComplete,
        Ready  // Acquired by render thread
    };
    /**
     * When entry is marked as `Ready`, asset thread will not modify the contents of this struct further.
     */

    // Populated in asset loading thread. Used by game thread
    std::filesystem::path source{};
    Render::WillModel data{};

    // Populated in TaskExecute, consumed in ThreadExecute
    std::vector<VkSamplerCreateInfo> pendingSamplerInfos;
    std::vector<ktxTexture2*> pendingTextures;


    // todo: acquire ops move to the asset load thread
    // AcquireOperations modelAcquires{};


    // Only accessed by asset loading thread
    uint32_t refCount = 0;
    // std::vector<UploadStagingHandle> uploadStagingHandles{};
    // std::chrono::steady_clock::time_point loadStartTime;
    // std::chrono::steady_clock::time_point loadEndTime;
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_ASSET_H
