//
// Created by William on 2026-08-14.
//

#ifndef WILL_ENGINE_FONT_CURVE_LOAD_SLOT_H
#define WILL_ENGINE_FONT_CURVE_LOAD_SLOT_H

#include <semaphore>
#include <volk.h>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"

namespace Core
{
class MemoryManager;
}

namespace Engine
{
struct Font;
}

namespace Render
{
struct ResourceManager;
struct VulkanContext;
}

namespace AssetLoad
{
/** Uploads a font's Slug curve blob into megaFontCurveBuffer: mmap + decompress on the load thread, suballocate, staged copy on the transfer queue, release barrier recorded as an acquire op on the Font. */
class FontCurveLoadSlot
{
public:
    FontCurveLoadSlot();

    ~FontCurveLoadSlot();

    void Initialize(
        enki::TaskScheduler* scheduler,
        Render::VulkanContext* context,
        Render::ResourceManager* resourceManager,
        Core::MemoryManager* memoryManager,
        Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
        Core::InlineFunction<void(bool success, FontCurveSlotHandle slotHandle)> notifyCallback);

    void Launch(FontCurveSlotHandle slotHandle, UploadStaging* uploadStaging, Engine::Font* outputFont);

    void Clear();

    FontCurveSlotHandle slotHandle{};

    Engine::Font* outputFont{nullptr};
    UploadStaging* uploadStaging{nullptr};

private:
    bool LoadCurvesFromDisk();

    bool AllocateResidency() const;

    void UploadCurves(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait);

    struct LoadTask : enki::ITaskSet
    {
        FontCurveLoadSlot* loadSlot{nullptr};

        explicit LoadTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    LoadTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Render::VulkanContext* context{nullptr};
    Render::ResourceManager* resourceManager{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    SubmitContext transferSubmit{};

    Core::HeapArray<uint8_t> blobData{};

    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* doneSemaphore)> _requestDispatchCallback;
    Core::InlineFunction<void(bool success, FontCurveSlotHandle slotHandle)> _notifyCallback;
};
} // AssetLoad

#endif //WILL_ENGINE_FONT_CURVE_LOAD_SLOT_H
