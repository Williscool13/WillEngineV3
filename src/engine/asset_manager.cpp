//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include "asset-load/asset_load_thread.h"
#include "platform/paths.h"

namespace Engine
{
AssetManager::AssetManager(AssetLoad::AssetLoadThread* assetLoadThread, Render::ResourceManager* resourceManager)
    : assetLoadThread(assetLoadThread), resourceManager(resourceManager)
{
    auto errorPath = Platform::GetAssetPath() / "textures/error.ktx2";
    auto whitePath = Platform::GetAssetPath() / "textures/white.ktx2";


    whiteTextureHandle = textureAllocator.Add();
    assert(whiteTextureHandle.IsValid());
    Render::Texture& whiteTexture = textures[whiteTextureHandle.index];
    whiteTexture.selfHandle = whiteTextureHandle;
    whiteTexture.source = whitePath;
    whiteTexture.name = whitePath.stem().string();
    whiteTexture.loadState = Render::Texture::LoadState::NotLoaded;
    whiteTexture.refCount = 1;
    whiteTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    assert(whiteTexture.bindlessHandle.index == AssetLoad::WHITE_IMAGE_BINDLESS_INDEX);
    pathToTextureHandle[whitePath] = whiteTextureHandle;
    assetLoadThread->RequestTextureLoad(whiteTexture.selfHandle, &whiteTexture);

    errorTextureHandle = textureAllocator.Add();
    assert(errorTextureHandle.IsValid());
    Render::Texture& errorTexture = textures[errorTextureHandle.index];
    errorTexture.selfHandle = errorTextureHandle;
    errorTexture.source = errorPath;
    errorTexture.name = errorPath.stem().string();
    errorTexture.loadState = Render::Texture::LoadState::NotLoaded;
    errorTexture.refCount = 1;
    errorTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    assert(errorTexture.bindlessHandle.index == AssetLoad::ERROR_IMAGE_BINDLESS_INDEX);
    pathToTextureHandle[errorPath] = errorTextureHandle;
    assetLoadThread->RequestTextureLoad(errorTexture.selfHandle, &errorTexture);

    VkSamplerCreateInfo samplerCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };
    defaultSampler = assetLoadThread->CreateSampler(samplerCreateInfo);
    Render::BindlessSamplerHandle defaultSamplerHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(defaultSampler.handle);
    assert(defaultSamplerHandle.index == AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX);
}

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
            SPDLOG_INFO("[AssetManager] Model load succeeded: {}", modelComplete.model->name);
        }
        else {
            modelComplete.model->bufferAcquireOps.clear();
            modelComplete.model->imageAcquireOps.clear();
            modelComplete.model->modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
            SPDLOG_ERROR("[AssetManager] Model load failed: {}", modelComplete.model->name);
        }
    }

    AssetLoad::TextureComplete textureComplete{};
    while (assetLoadThread->ResolveTextureLoads(textureComplete)) {
        if (textureComplete.success) {
            stagingFrameBuffer.imageAcquireOperations.push_back(textureComplete.texture->acquireBarrier);

            textureComplete.texture->loadState = Render::Texture::LoadState::Loaded;
            SPDLOG_INFO("[AssetManager] Texture load succeeded: {} (bindless index: {})", textureComplete.texture->name, static_cast<uint32_t>(textureComplete.texture->bindlessHandle.index));
        }
        else {
            textureComplete.texture->loadState = Render::Texture::LoadState::NotLoaded;
            SPDLOG_ERROR("[AssetManager] Texture load failed: {}", textureComplete.texture->name);
        }
    }
}

void AssetManager::ResolveUnloads()
{
    AssetLoad::WillModelComplete modelComplete{};
    while (assetLoadThread->ResolveUnload(modelComplete)) {
        SPDLOG_INFO("[AssetManager] Model unload succeeded: {}", modelComplete.model->name);
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
        SPDLOG_INFO("[AssetManager] Texture unload succeeded: {}", textureComplete.texture->name);

        textureComplete.texture->source.clear();
        textureComplete.texture->name.clear();
        textureComplete.texture->loadState = Render::Texture::LoadState::NotLoaded;
        textureComplete.texture->selfHandle = TextureHandle::INVALID;
        if (textureComplete.texture->bindlessHandle.index != 0) {
            resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(textureComplete.texture->bindlessHandle);
        }
        textureComplete.texture->bindlessHandle = Render::BindlessTextureHandle::INVALID;

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
    texture.loadState = Render::Texture::LoadState::NotLoaded;
    texture.refCount = 1;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

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
    // todo: add to a queue that only pops after 3 FIF
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
