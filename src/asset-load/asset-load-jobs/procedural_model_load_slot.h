//
// Created by William on 2026-03-14.
//

#ifndef WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
#define WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
#include <semaphore>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"

#include "core/containers/inline_function.h"
#include "render/vulkan/vk_resources.h"
#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/memory/tlsf_allocator.h"
#include "render/shaders/model_interop.h"

namespace Core
{
class MemoryManager;
}

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
                    Core::MemoryManager* _memoryManager,
                    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore signalSemaphore)> _requestTransferDispatchCallback,
                    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal, VkSemaphore waitSemaphore)> _requestGraphicsDispatchCallback,
                    Core::InlineFunction<void(bool success, ProceduralModelSlotHandle slotHandle)> _notifyCallback);

    void Launch(ProceduralModelSlotHandle _slotHandle, UploadStaging* _uploadStaging, Engine::StaticModel* _outputModel);

    void Clear();

    bool GenerateGeometry();

    bool AllocateGPUResources() const;

    void PrepareUploadData();

    void UploadGeometry(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    /**
     * @return false if the mega BLAS buffer ran out of space.
     */
    bool BuildBLAS(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    ProceduralModelSlotHandle slotHandle{};

    Engine::StaticModel* outputModel{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    struct GenerateModelTask : enki::ITaskSet
    {
        ProceduralModelLoadSlot* loadSlot{nullptr};

        explicit GenerateModelTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    GenerateModelTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    SubmitContext transferSubmit{};
    SubmitContext graphicsSubmit{};
    VkSemaphore uploadCompleteSemaphore{VK_NULL_HANDLE};

    UnpackedStaticModel rawData{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore, VkSemaphore signalSemaphore)> _requestTransferDispatchCallback;
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore, VkSemaphore waitSemaphore)> _requestGraphicsDispatchCallback;
    Core::InlineFunction<void(bool success, ProceduralModelSlotHandle slotHandle)> _notifyCallback;

    VkDeviceSize _blasScratchSize{};

    bool FinalizeGeometry(Core::Span<const Engine::FullVertex> vertices, Core::Span<const uint32_t> indices);

    struct ModuleGroupRange
    {
        uint32_t indexStart{0};
        uint32_t indexCount{0};
        int32_t slot{0};
    };

    bool FinalizeGeometryGroups(Core::Span<const Engine::FullVertex> vertices, Core::Span<const uint32_t> indices, Core::Span<const ModuleGroupRange> groups);

    bool GenerateShapeVariant(Engine::ProceduralParams& params);

    // While set, FinalizeGeometry appends into these instead of building the model (module part accumulation)
    Core::Vector<Engine::FullVertex>* moduleSinkVertices{nullptr};
    Core::Vector<uint32_t>* moduleSinkIndices{nullptr};

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

    bool GenerateSpiralStaircase(const Engine::SpiralStaircaseParams& p);

    bool GenerateRing(const Engine::RingParams& p);

    bool GenerateWall(const Engine::WallParams& p);

    bool GenerateLattice(const Engine::LatticeParams& p);

    bool GenerateCorrugatedPanel(const Engine::CorrugatedPanelParams& p);

    bool GenerateModule(const Engine::ModuleParams& p);

    bool GenerateSpline(const Engine::SplineParams& p);

    bool GenerateText3D(const Engine::Text3DParams& p);
};
} // AssetLoad

#endif //WILL_ENGINE_PROCEDURAL_MODEL_LOAD_SLOT_H
