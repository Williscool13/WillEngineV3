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

void AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const
{
    AssetLoad::WillModelComplete modelComplete{};
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

    AssetLoad::TextureComplete textureComplete{};
    while (assetLoadThread->ResolveTextureLoads(textureComplete)) {
        if (textureComplete.success) {
            stagingFrameBuffer.imageAcquireOperations.push_back(textureComplete.texture->acquireBarrier);

            textureComplete.texture->loadState = Render::Texture::LoadState::Loaded;
            SPDLOG_INFO("[AssetManager] Texture load succeeded: {}", textureComplete.texture->source.string());
        }
        else {
            textureComplete.texture->loadState = Render::Texture::LoadState::NotLoaded;
            SPDLOG_ERROR("[AssetManager] Texture load failed: {}", textureComplete.texture->source.string());
        }
    }
}

void AssetManager::ResolveUnloads()
{
    AssetLoad::WillModelComplete modelComplete{};
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

    AssetLoad::TextureComplete textureComplete{};
    while (assetLoadThread->ResolveTextureUnload(textureComplete)) {
        SPDLOG_INFO("[AssetManager] Texture unload succeeded: {}", textureComplete.texture->source.string());

        textureComplete.texture->source.clear();
        textureComplete.texture->name.clear();
        textureComplete.texture->loadState = Render::Texture::LoadState::NotLoaded;
        textureComplete.texture->selfHandle = TextureHandle::INVALID;

        textureAllocator.Remove(textureComplete.textureHandle);
    }
}

TextureHandle AssetManager::LoadTexture(const std::filesystem::path& path)
{
    auto it = pathToTextureHandle.find(path);
    if (it != pathToTextureHandle.end()) {
        TextureHandle existingHandle = it->second;
        if (textureAllocator.IsValid(existingHandle)) {
            Render::Texture& texture = textures[existingHandle.index];
            texture.refCount++;
            SPDLOG_TRACE("[AssetManager] Texture already loaded: {}, refCount: {}", path.string(), texture.refCount);
            return existingHandle;
        }
        pathToTextureHandle.erase(it);
    }

    TextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        SPDLOG_ERROR("[AssetManager] Failed to allocate texture slot for: {}", path.string());
        return TextureHandle{};
    }

    Render::Texture& texture = textures[handle.index];
    texture.selfHandle = handle;
    texture.source = path;
    texture.name = path.stem().string();
    texture.refCount = 1;
    texture.loadState = Render::Texture::LoadState::NotLoaded;

    pathToTextureHandle[path] = handle;

    assetLoadThread->RequestTextureLoad(texture.selfHandle, &texture);

    return handle;
}

Render::Texture* AssetManager::GetTexture(TextureHandle handle)
{
    if (!textureAllocator.IsValid(handle)) {
        return nullptr;
    }
    return &textures[handle.index];
}

void AssetManager::UnloadTexture(TextureHandle handle)
{
    if (!textureAllocator.IsValid(handle)) {
        SPDLOG_WARN("[AssetManager] Attempted to unload invalid texture handle");
        return;
    }

    Render::Texture& texture = textures[handle.index];
    texture.refCount--;

    if (texture.refCount == 0) {
        texture.loadState = Render::Texture::LoadState::NotLoaded;
        assetLoadThread->RequestTextureUnload(handle, &texture);
        pathToTextureHandle.erase(texture.source);
    }
}
} // Engine
