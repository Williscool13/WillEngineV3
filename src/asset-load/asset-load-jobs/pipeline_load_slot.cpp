//
// Created by William on 2026-01-19.
//

#include "pipeline_load_slot.h"

#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "spdlog/spdlog.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{

PipelineLoadSlot::PipelineLoadSlot() = default;

PipelineLoadSlot::~PipelineLoadSlot() = default;

void PipelineLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    VkPipelineCache _pipelineCache,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(bool success, PipelineSlotHandle slotHandle)> _notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    pipelineCache = _pipelineCache;
    memoryManager = _memoryManager;
    notifyCallback = std::move(_notifyCallback);
}

void PipelineLoadSlot::Launch(PipelineSlotHandle _pipelineSlotHandle, Render::PipelineData* _pipelineData)
{
    pipelineSlotHandle = _pipelineSlotHandle;
    pipelineData = _pipelineData;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void PipelineLoadSlot::Clear()
{
    pipelineSlotHandle = PipelineSlotHandle::INVALID;
    pipelineData = nullptr;
}

void PipelineLoadSlot::LoadPipelineTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    ZoneScopedN("LoadPipelineTask");

    if (!loadSlot || !loadSlot->pipelineData) {
        SPDLOG_ERROR("LoadPipelineTask: Invalid slot or pipeline data");
        if (loadSlot && loadSlot->notifyCallback) {
            loadSlot->notifyCallback(false, loadSlot->pipelineSlotHandle);
        }
        return;
    }

    bool success = false;
    {
        ZoneScopedN("CreatePipeline");
        success = loadSlot->pipelineData->CreatePipeline(loadSlot->context, loadSlot->memoryManager, loadSlot->pipelineCache);
    }

    if (loadSlot->notifyCallback) {
        loadSlot->notifyCallback(success, loadSlot->pipelineSlotHandle);
    }
}

} // AssetLoad