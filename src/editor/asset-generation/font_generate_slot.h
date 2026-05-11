//
// Created by William on 2026-05-11.
//

#ifndef WILL_ENGINE_FONT_GENERATE_SLOT_H
#define WILL_ENGINE_FONT_GENERATE_SLOT_H

#include <TaskScheduler.h>
#include <msdfgen/msdfgen-ext.h>

#include "core/containers/inline_function.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/memory/memory_manager.h"
#include "engine/core/font_id.h"

namespace Editor
{
using FontGenerateSlotHandle = Core::Handle<struct FontGenerateSlot>;

struct FontGenerateSlot
{
    FontGenerateSlot() = default;
    ~FontGenerateSlot();

    FontGenerateSlot(const FontGenerateSlot&) = delete;
    FontGenerateSlot& operator=(const FontGenerateSlot&) = delete;

    void Initialize(
        enki::TaskScheduler* scheduler,
        Core::MemoryManager* memoryManager,
        Core::InlineFunction<void(bool success, FontGenerateSlotHandle slotHandle)> notifyCallback
    );

    void Launch(FontGenerateSlotHandle slotHandle, const Core::Path& ttfPath, const Core::Path& outputPath, Engine::FontID fontId);

    void Clear();

    Core::Path ttfPath;
    Core::Path outputPath;
    Engine::FontID fontId{};

private:
    bool GenerateAndWrite();

    enki::TaskScheduler* scheduler{nullptr};
    Core::MemoryManager* memoryManager{nullptr};
    Core::InlineFunction<void(bool success, FontGenerateSlotHandle slotHandle)> _notifyCallback;
    msdfgen::FreetypeHandle* ft{nullptr};

    FontGenerateSlotHandle _slotHandle{FontGenerateSlotHandle::INVALID};

    struct GenerateTask : enki::ITaskSet
    {
        FontGenerateSlot* taskSlot{nullptr};

        explicit GenerateTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    GenerateTask task{};
};
} // namespace Editor

#endif //WILL_ENGINE_FONT_GENERATE_SLOT_H
