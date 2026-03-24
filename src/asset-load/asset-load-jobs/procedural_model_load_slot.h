//
// Created by William on 2026-03-14.
//

#ifndef WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
#define WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
#include <semaphore>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "render/shaders/model_interop.h"

namespace enki
{
class TaskScheduler;
}

namespace Render
{
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
class ProceduralModelLoadSlot
{
public:
    ProceduralModelLoadSlot();

    ~ProceduralModelLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager,
                    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> _requestDispatchCallback,
                    std::function<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback);

    void Launch(ProceduralModelSlotHandle _slotHandle, UploadStagingSlotHandle _uploadStagingSlotHandle, UploadStaging* _uploadStaging, Engine::StaticModel* _outputModel);

    void Clear();

    bool GenerateGeometry();

    bool AllocateGPUResources() const;


    void PrepareUploadData();

    void UploadGeometry(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait);

    ProceduralModelSlotHandle slotHandle{};
    UploadStagingSlotHandle uploadStagingSlotHandle{};

    Engine::StaticModel* outputModel{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct GenerateModelTask : enki::ITaskSet
    {
        ProceduralModelLoadSlot* loadSlot{nullptr};

        explicit GenerateModelTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    std::unique_ptr<GenerateModelTask> task{nullptr};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};

    RawStaticModel rawData{};
    std::vector<uint32_t> packedTriangles;

    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    std::function<void(bool success, ProceduralModelSlotHandle slotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> _notifyCallback;

    bool FinalizeGeometry(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    bool GenerateStaircase(const Engine::StaircaseParams& p);
    bool GenerateBox(const Engine::BoxParams& p);
    bool GenerateCylinder(const Engine::CylinderParams& p);
    bool GenerateCapsule(const Engine::CapsuleParams& p);
    bool GenerateTorus(const Engine::TorusParams& p);
    bool GenerateArch(const Engine::ArchParams& p);
    bool GenerateWedge(const Engine::WedgeParams& p);
    bool GenerateCone(const Engine::ConeParams& p);
    bool GenerateDoor(const Engine::DoorParams& p);
    bool GeneratePlane(const Engine::PlaneParams& p);
    bool GenerateSphere(const Engine::SphereParams& p);
    bool GenerateSubdividedSphere(const Engine::SubdividedSphereParams& p);
    bool GenerateHemisphere(const Engine::HemisphereParams& p);
    bool GeneratePipe(const Engine::PipeParams& p);
    bool GenerateTetrahedron(const Engine::TetrahedronParams& p);
    bool GenerateOctahedron(const Engine::OctahedronParams& p);
    bool GenerateIcosahedron(const Engine::IcosahedronParams& p);
    bool GenerateDodecahedron(const Engine::DodecahedronParams& p);
    bool GenerateKleinBottle(const Engine::KleinBottleParams& p);
    bool GenerateTrefoilKnot(const Engine::TrefoilKnotParams& p);
    bool GenerateCurvedRamp(const Engine::CurvedRampParams& p);
    bool GenerateBowl(const Engine::BowlParams& p);
    bool GenerateSpline(const Engine::SplineParams& p);
};
} // AssetLoad

#endif //WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
