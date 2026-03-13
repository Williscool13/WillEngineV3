//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include "asset-load/async_asset_load_manager.h"
#include "resources/scene/scene_format.h"
#include "editor/asset-generation/miscellaneous_asset_generate.h"
#include "logging/engine_log.h"
#include "platform/paths.h"
#include "render/resource_manager.h"

namespace Engine
{
AssetManager::AssetManager(Core::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager)
    : ctx(ctx), assetLoadManager(assetLoadManager), resourceManager(resourceManager)
{
#if WILL_EDITOR
    // Creates white/error if they don't exist.
    Editor::CreateCriticalEngineResources();
#endif

    ctx->bShouldRescanResources = true;
    Scan();

    Texture* whiteTex = LoadTexture(FindTextureByName("engine_default_white"));
    assert(whiteTex && whiteTex->bindlessHandle.index == WHITE_IMAGE_BINDLESS_INDEX);

    Texture* errorTex = LoadTexture(FindTextureByName("engine_default_error"));
    assert(errorTex && errorTex->bindlessHandle.index == ERROR_IMAGE_BINDLESS_INDEX);

    TextureID brdfLutID = FindTextureByName("brdf_lut");
    if (brdfLutID.IsValid()) {
        Texture* brdfTex = LoadTexture(brdfLutID);
        assert(brdfTex && brdfTex->bindlessHandle.index == BRDF_LUT_BINDLESS_INDEX);
    }
    else {
        // reserve, unused. Requires Engine restart
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Default brdf_lut does not exist, please regenerate and restart the engine");
    }

    TextureID smilingFriendID = FindTextureByName("smiling_friend");
    if (smilingFriendID.IsValid()) {
        Texture* smilingTex = LoadTexture(smilingFriendID);
        assert(smilingTex && smilingTex->bindlessHandle.index == SMILING_FRIENDS_BINDLESS_INDEX);
    }
    else {
        // reserve, unused. Requires Engine restart
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Default smiling friend logo does not exist, please regenerate and restart the engine");
    }

    SamplerDesc defaultSamplerDesc{};
    defaultSamplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    defaultSamplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    defaultSamplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler* defaultSampler = LoadSampler(defaultSamplerDesc);
    assert(defaultSampler && defaultSampler->bindlessHandle.index == ASSET_SAMPLER_BINDLESS_INDEX);

    std::filesystem::path assetPath = Platform::GetAssetPath();
    cubemapCache["kloofendal"_sid].source = assetPath / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.ktx2";
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

const AssetManager::CachedModelMetadata* AssetManager::GetModelMetadata(StringID modelId) const
{
    auto it = modelCache.find(modelId);
    return it != modelCache.end() ? &it->second : nullptr;
}

const AssetManager::CachedSceneMetadata* AssetManager::GetSceneMetadata(StringID sceneId) const
{
    auto it = sceneCache.find(sceneId);
    return it != sceneCache.end() ? &it->second : nullptr;
}

StaticModelHandle AssetManager::LoadModel(StringID modelId)
{
    if (!modelCache.contains(modelId)) {
        LOG_ERROR(Asset, "Model '{}' not found in registry", modelId.ToString());
        return StaticModelHandle::INVALID;
    }

    auto it = modelIdToHandle.find(modelId);
    if (it != modelIdToHandle.end()) {
        StaticModelHandle existingHandle = it->second;
        if (modelAllocator.IsValid(existingHandle)) {
            StaticModel& model = models[existingHandle.index];
            model.refCount++;
            LOG_TRACE(Asset, "Model already loaded: {}, refCount: {}", modelId.ToString(), model.refCount);
            return existingHandle;
        }
        modelIdToHandle.erase(it);
    }

    StaticModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", modelId.ToString());
        return StaticModelHandle::INVALID;
    }

    StaticModel& model = models[handle.index];
    model.selfHandle = handle;
    model.name = modelCache[modelId].name;
    model.source = modelCache[modelId].source;
    model.modelId = modelId;
    model.refCount = 1;
    model.modelLoadState = StaticModel::ModelLoadState::NotLoaded;

    modelIdToHandle[modelId] = handle;

    assetLoadManager->RequestModelLoad(&model);

    return handle;
}

StaticModel* AssetManager::GetModel(StaticModelHandle handle)
{
    if (!modelAllocator.IsValid(handle)) {
        return nullptr;
    }
    return &models[handle.index];
}

void AssetManager::UnloadModel(StaticModelHandle handle)
{
    if (!modelAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid model handle");
        return;
    }

    StaticModel& model = models[handle.index];
    model.refCount--;

    LOG_TRACE(Asset, "Model refCount decremented: {}, refCount: {}", model.name, model.refCount);

    if (model.refCount == 0) {
        model.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
        // assetLoadThread->RequestModelUnload(handle, &model);
        modelIdToHandle.erase(model.modelId);
    }
}

ResolveLoadResult AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const
{
    ResolveLoadResult loadCounts{};
    AssetLoad::StaticModelLoadComplete complete{};
    while (assetLoadManager->TryDequeueModelComplete(complete)) {
        if (complete.bSuccess) {
            stagingFrameBuffer.bufferAcquireOperations.insert(stagingFrameBuffer.bufferAcquireOperations.end(),
                                                              complete.model->bufferAcquireOps.begin(),
                                                              complete.model->bufferAcquireOps.end());

            stagingFrameBuffer.imageAcquireOperations.insert(stagingFrameBuffer.imageAcquireOperations.end(),
                                                             complete.model->imageAcquireOps.begin(),
                                                             complete.model->imageAcquireOps.end());

            complete.model->bufferAcquireOps.clear();
            complete.model->imageAcquireOps.clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::Loaded;
            complete.model->acquireFrame = ctx->currentFrame;
            LOG_TRACE(Asset, "Model load succeeded: {}", complete.model->modelId.ToString());
            loadCounts.modelLoadedCount++;
        }
        else {
            complete.model->bufferAcquireOps.clear();
            complete.model->imageAcquireOps.clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::NotLoaded;
            LOG_ERROR(Asset, "Model load failed: {}", complete.model->modelId.ToString());
        }
    }

    AssetLoad::TextureLoadComplete textureComplete{};
    while (assetLoadManager->TryDequeueTextureComplete(textureComplete)) {
        if (textureComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.push_back(textureComplete.texture->acquireBarrier);

            textureComplete.texture->loadState = Texture::LoadState::Loaded;
            textureComplete.texture->acquireFrame = ctx->currentFrame;
            LOG_TRACE(Asset, "Texture load succeeded: {} (bindless index: {})", textureComplete.texture->name, static_cast<uint32_t>(textureComplete.texture->bindlessHandle.index));
            loadCounts.textureLoadedCount++;
        }
        else {
            textureComplete.texture->loadState = Texture::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Texture load failed: {}", textureComplete.texture->name);
        }
    }

    AssetLoad::CubemapLoadComplete cubemapComplete{};
    while (assetLoadManager->TryDequeueCubemapComplete(cubemapComplete)) {
        if (cubemapComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.push_back(cubemapComplete.cubemap->acquireBarrier);

            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::Loaded;
            LOG_TRACE(Asset, "Cubemap load succeeded: {} (bindless index: {})", cubemapComplete.cubemap->name, static_cast<uint32_t>(cubemapComplete.cubemap->bindlessHandle.index));
            loadCounts.cubeLoadedCount++;
        }
        else {
            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Cubemap load failed: {}", cubemapComplete.cubemap->name);
        }
    }

    AssetLoad::SamplerLoadComplete samplerComplete{};
    while (assetLoadManager->TryDequeueSamplerComplete(samplerComplete)) {
        if (samplerComplete.bSuccess) {
            LOG_TRACE(Asset, "Sampler load succeeded (bindless index: {})", static_cast<uint32_t>(samplerComplete.sampler->bindlessHandle.index));
            loadCounts.samplerLoadedCount++;
        }
        else {
            LOG_ERROR(Asset, "Sampler load failed");
        }
    }

    return loadCounts;
}

void AssetManager::ResolveUnloads()
{
    const uint64_t currentFrame = ctx->currentFrame;

    for (auto& texture : textures) {
        if (!textureAllocator.IsValid(texture.selfHandle)) { continue; }
        if (texture.refCount > 0 || texture.retireFrame == 0 || currentFrame < texture.retireFrame) { continue; }
        if (texture.loadState != Texture::LoadState::Loaded) { continue; }

        LOG_TRACE(Asset, "Texture unloaded: {} (bindless index: {})", texture.name, static_cast<uint32_t>(texture.bindlessHandle.index));
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(texture.bindlessHandle);
        textureIdToHandle.erase(texture.textureId);
        textureAllocator.Remove(texture.selfHandle);
        texture = {};
    }

    for (auto& sampler : samplers) {
        if (!samplerAllocator.IsValid(sampler.selfHandle)) { continue; }
        if (sampler.refCount > 0 || sampler.retireFrame == 0 || currentFrame < sampler.retireFrame) { continue; }

        LOG_TRACE(Asset, "Sampler unloaded (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index));
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseSamplerBinding(sampler.bindlessHandle);
        samplerIdToHandle.erase(sampler.id);
        samplerAllocator.Remove(sampler.selfHandle);
        sampler = {};
    }
}

void AssetManager::Scan()
{
    bool expectedRescan = true;
    if (ctx->bShouldRescanResources.compare_exchange_strong(expectedRescan, false, std::memory_order::acq_rel, std::memory_order::relaxed)) {
        std::filesystem::path assetPath = Platform::GetAssetPath();
        if (std::filesystem::exists(assetPath)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
                const auto& path = entry.path();
                const auto ext = path.extension();

                if (ext == ".wtexture") {
                    auto header = ReadWTextureHeader(path);
                    if (!header) { continue; }
                    TextureID id{header->textureId};
                    const std::string name{header->name};
                    if (textureNameToId.contains(name) && textureNameToId.at(name) != id) {
                        auto pathName = path.string();
                        LOG_CRITICAL(Asset, "2 Textures were mounted that contain the same name. This will cause issues for texture lookups by name. ({})", pathName);
                    }
                    CachedTextureMetadata& cached = textureCache[id];
                    cached.source = path;
                    memcpy(cached.name, header->name, WTEXTURE_NAME_LENGTH);
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    textureNameToId[name] = id;
                }
                else if (ext == ".wsmesh") {
                    auto info = ReadWStaticModelInfo(path);
                    if (!info) { continue; }
                    const std::string stem = path.stem().string();
                    StringID id{stem.c_str(), stem.size()};

                    std::string name{info->header.name};
                    if (modelNameToId.contains(name) && modelNameToId.at(name) != id) {
                        auto pathName = path.string();
                        LOG_CRITICAL(Asset, "2 Models were mounted that contain the same name. This will cause issues for model lookups by name. ({})", pathName);
                    }

                    CachedModelMetadata& cached = modelCache[id];
                    cached.source = path;
                    cached.name = std::move(name);
                    cached.nodeCount = info->header.nodeCount;
                    cached.meshNodesCount = info->header.meshNodeCount;
                    cached.nodes = std::move(info->nodes);
                    modelNameToId[cached.name] = id;
                }
                else if (ext == ".wscene") {
                    auto header = ReadWSceneHeader(path);
                    if (!header) { continue; }
                    const std::string stem = path.stem().string();
                    StringID id{stem.c_str(), stem.size()};
                    CachedSceneMetadata& cached = sceneCache[id];
                    cached.source = path;
                    cached.sceneName = header->name;
                    cached.entityCount = header->entityCount;
                }
            }
        }
    }
}

Texture* AssetManager::LoadTexture(TextureID textureId)
{
    if (!textureCache.contains(textureId)) {
        LOG_ERROR(Asset, "Texture {:x} not found in registry", textureId.id);
        return nullptr;
    }

    auto it = textureIdToHandle.find(textureId);
    if (it != textureIdToHandle.end()) {
        TextureHandle existingHandle = it->second;
        if (textureAllocator.IsValid(existingHandle)) {
            Texture& texture = textures[existingHandle.index];
            texture.refCount++;
            texture.retireFrame = 0;
            LOG_TRACE(Asset, "Texture already loaded: {}, refCount: {}", texture.name, texture.refCount);
            return &texture;
        }
        textureIdToHandle.erase(it);
    }

    TextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate texture slot for {:x}", textureId.id);
        return nullptr;
    }

    const CachedTextureMetadata& meta = textureCache[textureId];

    Texture& texture = textures[handle.index];
    texture.selfHandle = handle;
    texture.source = meta.source;
    texture.textureId = textureId;
    memcpy(texture.name, meta.name, WTEXTURE_NAME_LENGTH);
    texture.width = meta.width;
    texture.height = meta.height;
    texture.mipCount = meta.mipCount;
    texture.dataOffset = meta.dataOffset;
    texture.dataSize = meta.dataSize;
    texture.uncompressedSize = meta.uncompressedSize;
    texture.loadState = Texture::LoadState::Loading;
    texture.refCount = 1;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    textureIdToHandle[textureId] = handle;

    assetLoadManager->RequestTextureLoad(&texture);

    return &texture;
}

void AssetManager::UnloadTexture(TextureID id)
{
    auto it = textureIdToHandle.find(id);
    if (it == textureIdToHandle.end()) {
        LOG_WARN(Asset, "Attempted to unload texture not in registry");
        return;
    }

    TextureHandle handle = it->second;
    if (!textureAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid texture handle");
        return;
    }

    Texture& texture = textures[handle.index];
    texture.refCount--;

    LOG_TRACE(Asset, "Texture refCount decremented: {}, refCount: {}", texture.name, texture.refCount);

    if (texture.refCount == 0) {
        texture.retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT * 4;
    }
}

Sampler* AssetManager::LoadSampler(SamplerDesc& samplerDesc)
{
    SamplerID id{fnv1a64(reinterpret_cast<uint8_t*>(&samplerDesc), sizeof(samplerDesc))};

    auto it = samplerIdToHandle.find(id);
    if (it != samplerIdToHandle.end()) {
        SamplerHandle existingHandle = it->second;
        if (samplerAllocator.IsValid(existingHandle)) {
            Sampler& existing = samplers[existingHandle.index];
            existing.refCount++;
            existing.retireFrame = 0;
            return &existing;
        }
        samplerIdToHandle.erase(it);
    }

    SamplerHandle handle = samplerAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate sampler slot");
        return nullptr;
    }

    Sampler& sampler = samplers[handle.index];
    sampler.desc = samplerDesc;
    sampler.id = id;
    sampler.selfHandle = handle;
    sampler.refCount = 1;
    sampler.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateSampler();

    samplerIdToHandle[id] = handle;

    assetLoadManager->RequestSamplerLoad(&sampler);

    return &sampler;
}

void AssetManager::UnloadSampler(SamplerDesc& desc)
{
    SamplerID id{fnv1a64(reinterpret_cast<uint8_t*>(&desc), sizeof(desc))};

    auto it = samplerIdToHandle.find(id);
    if (it == samplerIdToHandle.end()) {
        LOG_WARN(Asset, "Attempted to unload sampler not in registry");
        return;
    }

    SamplerHandle handle = it->second;
    if (!samplerAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid sampler handle");
        return;
    }

    Sampler& sampler = samplers[handle.index];
    sampler.refCount--;

    LOG_TRACE(Asset, "Sampler refCount decremented: {}, refCount: {}", sampler.id.id, sampler.refCount);

    if (sampler.refCount == 0) {
        sampler.retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT * 4;
    }
}

CubemapHandle AssetManager::LoadCubemap(StringID cubemapId)
{
    if (!cubemapCache.contains(cubemapId)) {
        LOG_ERROR(Asset, "Cubemap '{}' not found in registry", cubemapId.ToString());
        return CubemapHandle::INVALID;
    }

    auto it = cubemapIdToHandle.find(cubemapId);
    if (it != cubemapIdToHandle.end()) {
        CubemapHandle existingHandle = it->second;
        if (cubemapAllocator.IsValid(existingHandle)) {
            cubemaps[existingHandle.index].refCount++;
            LOG_TRACE(Asset, "Cubemap already loaded: {}, refCount: {}", cubemapId.ToString(), cubemaps[existingHandle.index].refCount);
            return existingHandle;
        }
        cubemapIdToHandle.erase(it);
    }

    CubemapHandle handle = cubemapAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate cubemap slot for: {}", cubemapId.ToString());
        return CubemapHandle::INVALID;
    }

    const std::filesystem::path& path = cubemapCache[cubemapId].source;
    Render::Cubemap& cubemap = cubemaps[handle.index];
    cubemap.source = path;
    cubemap.name = path.stem().string();
    cubemap.cubemapId = cubemapId;
    cubemap.refCount = 1;
    cubemap.loadState = Render::Cubemap::LoadState::Loading;
    cubemap.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateCubemap();

    cubemapIdToHandle[cubemapId] = handle;

    assetLoadManager->RequestCubemapLoad(&cubemap);

    return handle;
}

Render::Cubemap* AssetManager::GetCubemap(CubemapHandle handle)
{
    if (!cubemapAllocator.IsValid(handle)) {
        return nullptr;
    }
    return &cubemaps[handle.index];
}

void AssetManager::UnloadCubemap(CubemapHandle handle)
{
    if (!cubemapAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid cubemap handle");
        return;
    }

    Render::Cubemap& cubemap = cubemaps[handle.index];
    cubemap.refCount--;

    LOG_TRACE(Asset, "Cubemap refCount decremented: {}, refCount: {}", cubemap.name, cubemap.refCount);

    if (cubemap.refCount == 0) {
        cubemap.loadState = Render::Cubemap::LoadState::NotLoaded;
        // assetLoadThread->RequestCubemapUnload(handle, &cubemap);
        cubemapIdToHandle.erase(cubemap.cubemapId);
    }
}

TextureID AssetManager::FindTextureByName(std::string_view name) const
{
    auto it = textureNameToId.find(std::string(name));
    return it != textureNameToId.end() ? it->second : TextureID::INVALID;
}
} // Engine
