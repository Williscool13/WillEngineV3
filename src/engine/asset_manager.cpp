//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include "asset-load/asset_load_thread.h"

namespace Engine
{
AssetManager::AssetManager(AssetLoad::AssetLoadThread* assetLoadThread)
    : assetLoadThread(assetLoadThread)
{}

AssetManager::~AssetManager()
{
    for (auto& model : models) {
        if (modelAllocator.IsValid(model.selfHandle)) {
            model.refCount = 1;
            UnloadModel(model.selfHandle);
        }
    }
}

WillModelHandle AssetManager::LoadModel(const std::filesystem::path& path)
{
    auto it = pathToHandle.find(path);
    if (it != pathToHandle.end()) {
        WillModelHandle existingHandle = it->second;
        if (modelAllocator.IsValid(existingHandle)) {
            Render::WillModel& model = models[existingHandle.index];
            model.refCount++;
            SPDLOG_TRACE("[AssetManager] Model already loaded: {}, refCount: {}", path.string(), model.refCount);
            return existingHandle;
        }
        pathToHandle.erase(it);
    }

    WillModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_ERROR("[AssetManager] Failed to allocate model slot for: {}", path.string());
        return WillModelHandle{};
    }

    Render::WillModel& model = models[handle.index];
    model.selfHandle = handle;
    model.source = path;
    model.name = path.stem().string();
    model.refCount = 1;
    model.modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;

    pathToHandle[path] = handle;

    assetLoadThread->RequestLoad(model.selfHandle, &model);

    return handle;
}

Render::WillModel* AssetManager::GetModel(WillModelHandle handle)
{
    if (!modelAllocator.IsValid(handle)) {
        return nullptr;
    }
    return &models[handle.index];
}

void AssetManager::UnloadModel(WillModelHandle handle)
{
    if (!modelAllocator.IsValid(handle)) {
        SPDLOG_WARN("[AssetManager] Attempted to unload invalid model handle");
        return;
    }

    Render::WillModel& model = models[handle.index];
    model.refCount--;

    if (model.refCount == 0) {
        model.modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
        assetLoadThread->RequestUnLoad(handle, &model);
        pathToHandle.erase(model.source);
    }
}

void AssetManager::ResolveModelLoad(Core::FrameBuffer& stagingFrameBuffer)
{
    AssetLoad::WillModelComplete modelComplete;
    while (assetLoadThread->ResolveLoads(modelComplete)) {
        if (modelComplete.bSuccess) {
            stagingFrameBuffer.bufferAcquireOperations.insert(stagingFrameBuffer.bufferAcquireOperations.end(),
                                                              modelComplete.model->bufferAcquireOps.begin(),
                                                              modelComplete.model->bufferAcquireOps.end());

            stagingFrameBuffer.imageAcquireOperations.insert(stagingFrameBuffer.imageAcquireOperations.end(),
                                                             modelComplete.model->imageAcquireOps.begin(),
                                                             modelComplete.model->imageAcquireOps.end());

            modelComplete.model->bufferAcquireOps.clear();
            modelComplete.model->imageAcquireOps.clear();
            modelComplete.model->modelLoadState = Render::WillModel::ModelLoadState::Loaded;
            SPDLOG_INFO("[AssetManager] Model load succeeded: {}", modelComplete.model->source.string());
        }
        else {
            modelComplete.model->bufferAcquireOps.clear();
            modelComplete.model->imageAcquireOps.clear();
            modelComplete.model->modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
            SPDLOG_ERROR("[AssetManager] Model load failed: {}", modelComplete.model->source.string());
        }
    }
}

void AssetManager::ResolveModelUnload()
{
    AssetLoad::WillModelComplete modelComplete;
    while (assetLoadThread->ResolveUnload(modelComplete)) {
        SPDLOG_INFO("[AssetManager] Model unload succeeded: {}", modelComplete.model->source.string());
        modelComplete.model->modelData.Reset();
        modelComplete.model->bufferAcquireOps.clear();
        modelComplete.model->imageAcquireOps.clear();
        modelComplete.model->source.clear();
        modelComplete.model->name.clear();
        modelComplete.model->modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
        modelComplete.model->selfHandle = WillModelHandle::INVALID;

        modelAllocator.Remove(modelComplete.willModelHandle);
    }
}
} // Engine
