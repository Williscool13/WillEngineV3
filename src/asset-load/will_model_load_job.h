//
// Created by William on 2025-12-23.
//

#ifndef WILL_ENGINE_WILL_MODEL_LOAD_JOB_H
#define WILL_ENGINE_WILL_MODEL_LOAD_JOB_H
#include <span>

#include "asset_load_job.h"
#include "asset_load_types.h"
#include "ktx.h"
#include "engine/asset_manager_types.h"

namespace enki
{
class TaskScheduler;
}

namespace Render
{
struct WillModel;
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class WillModelLoadJob : public AssetLoadJob
{
public:
    WillModelLoadJob();

    WillModelLoadJob(Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkCommandBuffer commandBuffer);

    ~WillModelLoadJob() override;

    WillModelLoadJob(const WillModelLoadJob&) = delete;

    WillModelLoadJob& operator=(const WillModelLoadJob&) = delete;

    WillModelLoadJob(WillModelLoadJob&&) noexcept = default;

    WillModelLoadJob& operator=(WillModelLoadJob&&) noexcept = default;

    void StartJob() override;

    TaskState TaskExecute(enki::TaskScheduler* scheduler) override;

    bool PreThreadExecute() override;

    ThreadState ThreadExecute() override;

    bool PostThreadExecute() override;

    uint32_t GetUploadCount() override;

    void Reset() override;

    Engine::WillModelHandle willModelHandle{Engine::WillModelHandle::INVALID};
    Render::WillModel* outputModel{nullptr};

private:
    struct LoadModelTask : enki::ITaskSet
    {
        WillModelLoadJob* loadJob{nullptr};

        explicit LoadModelTask()
            : ITaskSet(1)
        {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
    };

    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    // Task
    TaskState taskState{TaskState::NotStarted};
    VkCommandBuffer commandBuffer;
    std::unique_ptr<LoadModelTask> task;
    UnpackedWillModel rawData{};
    std::vector<ktxTexture2*> pendingTextures;

    // Thread
    std::unique_ptr<UploadStaging> uploadStaging;
    /**
     * Cached vector to store SkinnedVertex->Vertex for non-skinned models.
     */
    std::vector<Vertex> convertedVertices;
    /**
     * Cached vector to store 3x uint8_t->1x uint32_t for meshlet triangles.
     */
    std::vector<uint32_t> packedTriangles;
    uint32_t pendingTextureHead{0};
    bool bPendingPreCopyBarrier = true;
    bool bPendingFinalBarrier = true;
    uint32_t pendingMipHead{0};
    uint32_t pendingVerticesHead{0};
    uint32_t pendingMeshletVerticesHead{0};
    uint32_t pendingMeshletTrianglesHead{0};
    uint32_t pendingMeshletsHead{0};
    uint32_t pendingPrimitivesHead{0};
    uint32_t pendingBufferBarrier{0};

    uint32_t uploadCount{0};
};
} // AssetLoad

#endif //WILL_ENGINE_WILL_MODEL_LOAD_JOB_H
