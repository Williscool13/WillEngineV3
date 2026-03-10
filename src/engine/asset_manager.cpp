//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include <fstream>

#include "asset-load/async_asset_load_manager.h"
#include "core/hash/fnv_1_a.h"
#include "engine/textures/texture_format.h"
#include "logging/engine_log.h"
#include "platform/paths.h"
#include "render/model/model_serialization.h"

namespace Engine
{
AssetManager::AssetManager(AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager)
    : assetLoadManager(assetLoadManager), resourceManager(resourceManager)
{
    auto errorPath = Platform::GetAssetPath() / "textures/error.ktx2";
    auto whitePath = Platform::GetAssetPath() / "textures/white.ktx2";
    auto brdfLutPath = Platform::GetAssetPath() / "textures/brdf_lut.ktx2";


    whiteTextureHandle = textureAllocator.Add();
    assert(whiteTextureHandle.IsValid());
    Texture& whiteTexture = textures[whiteTextureHandle.index];
    whiteTexture.selfHandle = whiteTextureHandle;
    whiteTexture.source = whitePath;
    strncpy_s(whiteTexture.name, whitePath.stem().string().c_str(), WTEXTURE_NAME_LENGTH - 1);
    whiteTexture.textureId = TextureID(fnv1a64("white", 5));
    whiteTexture.loadState = Texture::LoadState::NotLoaded;
    whiteTexture.refCount = 1;
    whiteTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    assert(whiteTexture.bindlessHandle.index == WHITE_IMAGE_BINDLESS_INDEX);
    assetLoadManager->RequestTextureLoad(&whiteTexture);

    errorTextureHandle = textureAllocator.Add();
    assert(errorTextureHandle.IsValid());
    Texture& errorTexture = textures[errorTextureHandle.index];
    errorTexture.selfHandle = errorTextureHandle;
    errorTexture.source = errorPath;
    strncpy_s(errorTexture.name, errorPath.stem().string().c_str(), WTEXTURE_NAME_LENGTH - 1);
    errorTexture.textureId = TextureID(fnv1a64("error", 5));
    errorTexture.loadState = Texture::LoadState::NotLoaded;
    errorTexture.refCount = 1;
    errorTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    assert(errorTexture.bindlessHandle.index == ERROR_IMAGE_BINDLESS_INDEX);
    assetLoadManager->RequestTextureLoad(&errorTexture);

    brdfLutTextureHandle = textureAllocator.Add();
    assert(brdfLutTextureHandle.IsValid());
    Texture& brdfLutTexture = textures[brdfLutTextureHandle.index];
    brdfLutTexture.selfHandle = brdfLutTextureHandle;
    brdfLutTexture.source = brdfLutPath;
    strncpy_s(brdfLutTexture.name, brdfLutPath.stem().string().c_str(), WTEXTURE_NAME_LENGTH - 1);
    brdfLutTexture.textureId = TextureID(fnv1a64("brdf_lut", 8));
    brdfLutTexture.loadState = Texture::LoadState::NotLoaded;
    brdfLutTexture.refCount = 1;
    brdfLutTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    assert(brdfLutTexture.bindlessHandle.index == BRDF_LUT_BINDLESS_INDEX);
    assetLoadManager->RequestTextureLoad(&brdfLutTexture);

    VkSamplerCreateInfo samplerCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
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

    defaultSampler = Render::Sampler::CreateSampler(resourceManager->context, samplerCreateInfo);
    Render::BindlessSamplerHandle defaultSamplerHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.AllocateSampler(defaultSampler.handle);
    assert(defaultSamplerHandle.index == ASSET_SAMPLER_BINDLESS_INDEX);


    std::filesystem::path assetPath = Platform::GetAssetPath();
    modelRegistry["stanford_dragon"_sid] = assetPath / "dragon/dragon.willmodel";
    modelRegistry["4k_box"_sid] = assetPath / "BoxTextured4k.willmodel";
    modelRegistry["sphere"_sid] = assetPath / "Sphere.willmodel";
    modelRegistry["sponza"_sid] = assetPath / "sponza2/sponza.willmodel";
    modelRegistry["intel_sponza"_sid] = assetPath / "IntelSponza.willmodel";
    modelRegistry["portal_plane"_sid] = assetPath / "Plane.willmodel";

    for (const auto& [id, path] : modelRegistry) {
        Render::ModelReader reader(path);
        if (reader.GetSuccessfullyLoaded()) {
            CachedModelMetadata& cached = modelMetadataCache[id];
            cached.counts = reader.GetMetadata();
            reader.ReadNodes(cached.nodes);
        }
        else {
            LOG_WARN(Asset, "Failed to read metadata for model '{}'", id.ToString());
        }
    }

    std::filesystem::path texturesPath = assetPath / "textures";
    if (std::filesystem::exists(texturesPath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(texturesPath)) {
            if (entry.path().extension() != ".wtexture") { continue; }
            std::ifstream f(entry.path(), std::ios::binary);
            std::optional<WTextureHeader> header = ReadWTextureHeader(f);
            if (!header) { continue; }
            TextureID id{header->textureId};
            const std::string name{header->name};
            assert(!textureNameToId.contains(name) && "Duplicate .wtexture name detected");
            textureRegistry[id] = entry.path();
            textureNameToId[name] = id;
        }
    }

    cubemapRegistry["kloofendal"_sid] = assetPath / "environment-map/kloofendal_48d_partly_cloudy_puresky_4k.ktx2";

    std::filesystem::path scenesPath = assetPath / "scenes";
    if (std::filesystem::exists(scenesPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(scenesPath)) {
            if (entry.path().extension() == ".wscene") {
                RegisterScene(entry.path());
            }
        }
    }
}

void AssetManager::RegisterScene(const std::filesystem::path& path)
{
    std::string stem = path.stem().string();
    sceneRegistry[StringID(stem.c_str(), stem.size())] = path;
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
    auto it = modelMetadataCache.find(modelId);
    return it != modelMetadataCache.end() ? &it->second : nullptr;
}

WillModelHandle AssetManager::LoadModel(StringID modelId)
{
    if (!modelRegistry.contains(modelId)) {
        LOG_ERROR(Asset, "Model '{}' not found in registry", modelId.ToString());
        return WillModelHandle::INVALID;
    }

    auto it = modelIdToHandle.find(modelId);
    if (it != modelIdToHandle.end()) {
        WillModelHandle existingHandle = it->second;
        if (modelAllocator.IsValid(existingHandle)) {
            Render::WillModel& model = models[existingHandle.index];
            model.refCount++;
            LOG_TRACE(Asset, "Model already loaded: {}, refCount: {}", modelId.ToString(), model.refCount);
            return existingHandle;
        }
        modelIdToHandle.erase(it);
    }

    WillModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", modelId.ToString());
        return WillModelHandle::INVALID;
    }

    Render::WillModel& model = models[handle.index];
    model.selfHandle = handle;
    model.source = modelRegistry[modelId];
    model.modelId = modelId;
    model.refCount = 1;
    model.modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;

    modelIdToHandle[modelId] = handle;

    assetLoadManager->RequestModelLoad(&model);

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
        LOG_WARN(Asset, "Attempted to unload invalid model handle");
        return;
    }

    Render::WillModel& model = models[handle.index];
    model.refCount--;

    if (model.refCount == 0) {
        model.modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
        // assetLoadThread->RequestModelUnload(handle, &model);
        modelIdToHandle.erase(model.modelId);
    }
}

ResolveLoadResult AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const
{
    ResolveLoadResult loadCounts{};
    AssetLoad::WillModelLoadComplete complete{};
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
            complete.model->modelLoadState = Render::WillModel::ModelLoadState::Loaded;
            LOG_INFO(Asset, "Model load succeeded: {}", complete.model->modelId.ToString());
            loadCounts.modelLoadedCount++;
        }
        else {
            complete.model->bufferAcquireOps.clear();
            complete.model->imageAcquireOps.clear();
            complete.model->modelLoadState = Render::WillModel::ModelLoadState::NotLoaded;
            LOG_ERROR(Asset, "Model load failed: {}", complete.model->modelId.ToString());
        }
    }

    AssetLoad::TextureLoadComplete textureComplete{};
    while (assetLoadManager->TryDequeueTextureComplete(textureComplete)) {
        if (textureComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.push_back(textureComplete.texture->acquireBarrier);

            textureComplete.texture->loadState = Texture::LoadState::Loaded;
            LOG_INFO(Asset, "Texture load succeeded: {} (bindless index: {})", textureComplete.texture->name, static_cast<uint32_t>(textureComplete.texture->bindlessHandle.index));
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
            LOG_INFO(Asset, "Cubemap load succeeded: {} (bindless index: {})", cubemapComplete.cubemap->name, static_cast<uint32_t>(cubemapComplete.cubemap->bindlessHandle.index));
            loadCounts.cubeLoadedCount++;
        }
        else {
            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Cubemap load failed: {}", cubemapComplete.cubemap->name);
        }
    }

    return loadCounts;
}

void AssetManager::ResolveUnloads()
{
    /*AssetLoad::WillModelComplete modelComplete{};
    while (assetLoadThread->ResolveModelUnload(modelComplete)) {
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
        textureComplete.texture->loadState = Engine::Texture::LoadState::NotLoaded;
        textureComplete.texture->selfHandle = TextureHandle::INVALID;
        if (textureComplete.texture->bindlessHandle.index != 0) {
            resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(textureComplete.texture->bindlessHandle);
        }
        textureComplete.texture->bindlessHandle = Render::BindlessTextureHandle::INVALID;

        textureAllocator.Remove(textureComplete.textureHandle);
    }*/
}

TextureHandle AssetManager::LoadTexture(TextureID textureId)
{
    if (!textureRegistry.contains(textureId)) {
        LOG_ERROR(Asset, "Texture {:x} not found in registry", textureId.id);
        return TextureHandle::INVALID;
    }

    auto it = textureIdToHandle.find(textureId);
    if (it != textureIdToHandle.end()) {
        TextureHandle existingHandle = it->second;
        if (textureAllocator.IsValid(existingHandle)) {
            Texture& texture = textures[existingHandle.index];
            texture.refCount++;
            LOG_TRACE(Asset, "Texture already loaded: {}, refCount: {}", texture.name, texture.refCount);
            return existingHandle;
        }
        textureIdToHandle.erase(it);
    }

    TextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate texture slot for {:x}", textureId.id);
        return TextureHandle::INVALID;
    }

    const std::filesystem::path& path = textureRegistry[textureId];
    assert(path.extension() == ".wtexture");

    std::ifstream f(path, std::ios::binary);
    auto header = ReadWTextureHeader(f);
    if (!header) {
        LOG_ERROR(Asset, "Failed to read header for texture {:x}", textureId.id);
        textureAllocator.Remove(handle);
        return TextureHandle::INVALID;
    }

    Texture& texture = textures[handle.index];
    texture.selfHandle = handle;
    texture.source     = path;
    texture.textureId  = textureId;
    memcpy(texture.name, header->name, WTEXTURE_NAME_LENGTH);
    texture.width      = header->width;
    texture.height     = header->height;
    texture.mipCount   = header->mipCount;
    texture.dataOffset = header->dataOffset;
    texture.dataSize   = header->dataSize;
    texture.loadState  = Texture::LoadState::Loading;
    texture.refCount   = 1;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    textureIdToHandle[textureId] = handle;

    assetLoadManager->RequestTextureLoad(&texture);

    return handle;
}

Texture* AssetManager::GetTexture(TextureHandle handle)
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
        LOG_WARN(Asset, "Attempted to unload invalid texture handle");
        return;
    }

    Texture& texture = textures[handle.index];
    texture.refCount--;

    LOG_TRACE(Asset, "Texture refCount decremented: {}, refCount: {}", texture.name, texture.refCount);

    if (texture.refCount == 0) {
        texture.loadState = Texture::LoadState::NotLoaded;
        // assetLoadThread->RequestTextureUnload(handle, &texture);
        textureIdToHandle.erase(texture.textureId);
    }
}

CubemapHandle AssetManager::LoadCubemap(StringID cubemapId)
{
    if (!cubemapRegistry.contains(cubemapId)) {
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

    const std::filesystem::path& path = cubemapRegistry[cubemapId];
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
