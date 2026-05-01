//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include "asset-load/async_asset_load_manager.h"
#include "resources/environment_map/environment_map_format.h"
#include "resources/prefab/prefab_format.h"
#include "resources/scene/scene_format.h"
#include "editor/asset-generation/miscellaneous_asset_generate.h"
#include "logging/engine_log.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "resources/model/model_format.h"

namespace Engine
{
AssetManager::AssetManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager)
    : memoryManager(&memoryManager), ctx(ctx), assetLoadManager(assetLoadManager), resourceManager(resourceManager),
      modelNameToId(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_MODELS),
      modelCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_MODELS),
      textureNameToId(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_TEXTURES),
      textureCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_TEXTURES),
      cubemapNameToId(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_CUBEMAPS),
      cubemapCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_CUBEMAPS),
      sceneCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_SCENES),
      prefabCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_PREFABS)
{
#if WILL_EDITOR
    // Creates white/error if they don't exist. Also creates BRDF LUT
    Editor::CreateCriticalEngineResources(&memoryManager);
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

    TextureID smaaArea = FindTextureByName("smaa_area");
    if (smaaArea.IsValid()) {
        Texture* smaaAreaTex = LoadTexture(smaaArea);
        assert(smaaAreaTex && smaaAreaTex->bindlessHandle.index == SMAA_AREA_BINDLESS_INDEX);
    }
    else {
        // reserve, unused. Requires Engine restart
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Default SMAA Area logo does not exist, please regenerate and restart the engine");
    }
    TextureID smaaSearch = FindTextureByName("smaa_search");
    if (smaaSearch.IsValid()) {
        Texture* smaaSearchTex = LoadTexture(smaaSearch);
        assert(smaaSearchTex && smaaSearchTex->bindlessHandle.index == SMAA_SEARCH_BINDLESS_INDEX);
    }
    else {
        // reserve, unused. Requires Engine restart
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Default SMAA Search logo does not exist, please regenerate and restart the engine");
    }

    SamplerDesc defaultLinearSamplerDesc{};
    defaultLinearSamplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    defaultLinearSamplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    defaultLinearSamplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler* defaultLinear = LoadSampler(defaultLinearSamplerDesc);
    assert(defaultLinear && defaultLinear->bindlessHandle.index == ASSET_SAMPLER_LINEAR_BINDLESS_INDEX);

    SamplerDesc defaultNearestSamplerDesc{};
    defaultNearestSamplerDesc.magFilter = VK_FILTER_NEAREST;
    defaultNearestSamplerDesc.minFilter = VK_FILTER_NEAREST;
    defaultNearestSamplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    defaultNearestSamplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    defaultNearestSamplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    Sampler* defaultNearest = LoadSampler(defaultNearestSamplerDesc);
    assert(defaultNearest && defaultNearest->bindlessHandle.index == ASSET_SAMPLER_NEAREST_BINDLESS_INDEX);
}


AssetManager::~AssetManager()
{
    for (auto& model : models) {
        if (!modelAllocator.IsValid(model.selfHandle)) { continue; }
        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded) {
            model.modelData.Reset(resourceManager);
        }
    }
}

const AssetManager::CachedSceneMetadata* AssetManager::GetSceneMetadata(StringID sceneId) const
{
    return sceneCache.Find(sceneId);
}

void AssetManager::RegisterScene(StringID sceneId, const char* sceneName)
{
    CachedSceneMetadata& cached = sceneCache[sceneId];
    cached.sceneName = Core::InlineString<128>(sceneName);
    cached.entityCount = 0;
}

void AssetManager::UpdateSceneCachePath(StringID sceneId, const Core::Path& path, uint32_t entityCount)
{
    auto it = sceneCache.Find(sceneId);
    if (!it) {
        return;
    }
    it->source = path;
    it->entityCount = entityCount;
}

const AssetManager::CachedPrefabMetadata* AssetManager::GetPrefabMetadata(StringID prefabId) const
{
    return prefabCache.Find(prefabId);
}

bool AssetManager::DeleteScene(StringID sceneId)
{
    const CachedSceneMetadata* it = sceneCache.Find(sceneId);
    if (!it) { return false; }
    if (!it->source.IsEmpty()) {
        Platform::DeleteSingleFile(it->source.c_str());
    }
    sceneCache.Remove(sceneId);
    return true;
}

bool AssetManager::DeletePrefab(StringID prefabId)
{
    const CachedPrefabMetadata* it = prefabCache.Find(prefabId);
    if (!it) { return false; }
    if (!it->source.IsEmpty()) {
        Platform::DeleteSingleFile(it->source.c_str());
    }
    prefabCache.Remove(prefabId);
    return true;
}

StaticModelHandle AssetManager::LoadModel(ModelID modelId)
{
    if (!modelCache.Contains(modelId)) {
        LOG_ERROR(Asset, "Model '{}' not found in registry", modelId.id);
        return StaticModelHandle::INVALID;
    }

    StaticModelHandle* existingPtr = modelIdToHandle.Find(modelId);
    if (existingPtr != nullptr) {
        StaticModelHandle existingHandle = *existingPtr;
        if (modelAllocator.IsValid(existingHandle)) {
            StaticModel& model = models[existingHandle.index];
            model.refCount++;
            model.retireFrame = 0;
            LOG_TRACE(Asset, "Model already loaded: {}, refCount: {}", model.name.c_str(), model.refCount);
            return existingHandle;
        }
        modelIdToHandle.Remove(modelId);
    }

    StaticModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", modelCache[modelId].name.c_str());
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

    LOG_TRACE(Asset, "Requesting model load: {}", model.name.c_str());
    assetLoadManager->RequestModelLoad(&model);

    return handle;
}

StaticModelHandle AssetManager::LoadProceduralModel(ProceduralParams& params)
{
    const size_t idx = params.index();
    uint64_t hash = fnv1a64(reinterpret_cast<const uint8_t*>(&idx), sizeof(idx));
    // The fuck is this shit what the fuck
    std::visit([&hash](const auto& v) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, std::monostate>) {
            hash = fnv1a64(reinterpret_cast<const uint8_t*>(&v), sizeof(v), hash);
        }
    }, params);

    ModelID proceduralModelId{hash};

    StaticModelHandle* existingPtr = modelIdToHandle.Find(proceduralModelId);
    if (existingPtr != nullptr) {
        StaticModelHandle existingHandle = *existingPtr;
        if (modelAllocator.IsValid(existingHandle)) {
            StaticModel& model = models[existingHandle.index];
            model.refCount++;
            model.retireFrame = 0;
            LOG_TRACE(Asset, "Procedural model already loaded: {}, refCount: {}", model.name.c_str(), model.refCount);
            return existingHandle;
        }
        modelIdToHandle.Remove(proceduralModelId);
    }

    StaticModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", proceduralModelId.id);
        return StaticModelHandle::INVALID;
    }

    static int32_t proceduralCounter = 0;
    StaticModel& model = models[handle.index];
    model.selfHandle = handle;
    model.name = Core::InlineString<128>::Format("Procedural Mesh %d", proceduralCounter++);
    model.modelId = proceduralModelId;
    model.proceduralParams = params;
    model.refCount = 1;
    model.modelLoadState = StaticModel::ModelLoadState::NotLoaded;

    modelIdToHandle[proceduralModelId] = handle;

    LOG_TRACE(Asset, "Requesting procedural model load: {}", model.name.c_str());
    assetLoadManager->RequestProceduralModelLoad(&model);

    return handle;
}

StaticModelHandle AssetManager::LoadSplineModel(const SplineParams& params)
{
    uint64_t hash = fnv1a64(reinterpret_cast<const uint8_t*>(params.spline.points.Data()), params.spline.points.Size() * sizeof(Vec3));
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.spline.mode), sizeof(params.spline.mode), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.spline.bClosed), sizeof(params.spline.bClosed), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(params.spline.rolls.Data()), params.spline.rolls.Size() * sizeof(float), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.radius), sizeof(params.radius), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.rollAngle), sizeof(params.rollAngle), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.sides), sizeof(params.sides), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.segmentsPerSpan), sizeof(params.segmentsPerSpan), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.bCaps), sizeof(params.bCaps), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.bDualPath), sizeof(params.bDualPath), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.dualPathSpacing), sizeof(params.dualPathSpacing), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.bCrossPlanks), sizeof(params.bCrossPlanks), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&params.crossPlankInterval), sizeof(params.crossPlankInterval), hash);

    ModelID splineModelId{hash};

    StaticModelHandle* existingPtr = modelIdToHandle.Find(splineModelId);
    if (existingPtr != nullptr) {
        StaticModelHandle existingHandle = *existingPtr;
        if (modelAllocator.IsValid(existingHandle)) {
            StaticModel& model = models[existingHandle.index];
            model.refCount++;
            model.retireFrame = 0;
            LOG_TRACE(Asset, "Spline model already loaded: {}, refCount: {}", splineModelId.id, model.refCount);
            return existingHandle;
        }
        modelIdToHandle.Remove(splineModelId);
    }

    StaticModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for spline");
        return StaticModelHandle::INVALID;
    }

    static int32_t splineCounter = 0;
    StaticModel& model = models[handle.index];
    model.selfHandle = handle;
    model.name = Core::InlineString<128>::Format("Spline Mesh %d", splineCounter++);
    model.modelId = splineModelId;
    model.splineParams = params;
    model.refCount = 1;
    model.modelLoadState = StaticModel::ModelLoadState::NotLoaded;

    modelIdToHandle[splineModelId] = handle;

    LOG_TRACE(Asset, "Requesting spline model load: {}", model.name.c_str());
    assetLoadManager->RequestProceduralModelLoad(&model);
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

    LOG_TRACE(Asset, "Model refCount decremented: {}, refCount: {}", model.name.c_str(), model.refCount);

    if (model.refCount == 0) {
        model.retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT * 4;
    }
}

ResolveLoadResult AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const
{
    ResolveLoadResult loadCounts{};
    AssetLoad::StaticModelLoadComplete complete{};
    while (assetLoadManager->TryDequeueModelComplete(complete)) {
        if (complete.bSuccess) {
            stagingFrameBuffer.bufferAcquireOperations.Insert(stagingFrameBuffer.bufferAcquireOperations.end(),
                                                              complete.model->bufferAcquireOps.begin(),
                                                              complete.model->bufferAcquireOps.end());

            stagingFrameBuffer.imageAcquireOperations.Insert(stagingFrameBuffer.imageAcquireOperations.end(),
                                                             complete.model->imageAcquireOps.begin(),
                                                             complete.model->imageAcquireOps.end());

            complete.model->bufferAcquireOps.Clear();
            complete.model->imageAcquireOps.Clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::Loaded;
            complete.model->acquireFrame = ctx->currentFrame;
            LOG_TRACE(Asset, "Model load succeeded: {}", complete.model->name.c_str());
            loadCounts.modelLoadedCount++;
        }
        else {
            complete.model->bufferAcquireOps.Clear();
            complete.model->imageAcquireOps.Clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::NotLoaded;
            LOG_ERROR(Asset, "Model load failed: {}", complete.model->name.c_str());
        }
    }

    AssetLoad::StaticModelLoadComplete proceduralComplete{};
    while (assetLoadManager->TryDequeueProceduralModelComplete(proceduralComplete)) {
        if (proceduralComplete.bSuccess) {
            stagingFrameBuffer.bufferAcquireOperations.Insert(stagingFrameBuffer.bufferAcquireOperations.end(),
                                                              proceduralComplete.model->bufferAcquireOps.begin(),
                                                              proceduralComplete.model->bufferAcquireOps.end());

            proceduralComplete.model->bufferAcquireOps.Clear();
            proceduralComplete.model->imageAcquireOps.Clear();
            proceduralComplete.model->modelLoadState = StaticModel::ModelLoadState::Loaded;
            proceduralComplete.model->acquireFrame = ctx->currentFrame;
            LOG_TRACE(Asset, "Procedural model generation succeeded: {}", proceduralComplete.model->name.c_str());
            loadCounts.modelLoadedCount++;
        }
        else {
            proceduralComplete.model->bufferAcquireOps.Clear();
            proceduralComplete.model->imageAcquireOps.Clear();
            proceduralComplete.model->modelLoadState = StaticModel::ModelLoadState::NotLoaded;
            LOG_ERROR(Asset, "Procedural model generation failed: {}", proceduralComplete.model->name.c_str());
        }
    }

    AssetLoad::TextureLoadComplete textureComplete{};
    while (assetLoadManager->TryDequeueTextureComplete(textureComplete)) {
        if (textureComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.PushBack(textureComplete.texture->acquireBarrier);

            textureComplete.texture->loadState = Texture::LoadState::Loaded;
            textureComplete.texture->acquireFrame = ctx->currentFrame;
            LOG_TRACE(Asset, "Texture load succeeded: {} (bindless index: {})", textureComplete.texture->name.c_str(), static_cast<uint32_t>(textureComplete.texture->bindlessHandle.index));
            loadCounts.textureLoadedCount++;
        }
        else {
            textureComplete.texture->loadState = Texture::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Texture load failed: {}", textureComplete.texture->name.c_str());
        }
    }

    AssetLoad::CubemapLoadComplete cubemapComplete{};
    while (assetLoadManager->TryDequeueCubemapComplete(cubemapComplete)) {
        if (cubemapComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.PushBack(cubemapComplete.cubemap->acquireBarrier);

            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::Loaded;
            LOG_TRACE(Asset, "Cubemap load succeeded: {} (bindless index: {})", cubemapComplete.cubemap->name.c_str(), static_cast<uint32_t>(cubemapComplete.cubemap->bindlessHandle.index));
            loadCounts.cubeLoadedCount++;
        }
        else {
            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Cubemap load failed: {}", cubemapComplete.cubemap->name.c_str());
        }
    }

    AssetLoad::SamplerLoadComplete samplerComplete{};
    while (assetLoadManager->TryDequeueSamplerComplete(samplerComplete)) {
        if (samplerComplete.bSuccess) {
            LOG_TRACE(Asset, "Sampler load succeeded (bindless index: {})", static_cast<uint32_t>(samplerComplete.sampler->bindlessHandle.index));
            samplerComplete.sampler->loadState = Sampler::LoadState::Loaded;
            loadCounts.samplerLoadedCount++;
        }
        else {
            samplerComplete.sampler->loadState = Sampler::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Sampler load failed");
        }
    }

    return loadCounts;
}

void AssetManager::ResolveUnloads()
{
    const uint64_t currentFrame = ctx->currentFrame;

    for (auto& model : models) {
        if (!modelAllocator.IsValid(model.selfHandle)) { continue; }
        if (model.refCount > 0 || model.retireFrame == 0 || currentFrame < model.retireFrame) { continue; }

        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded) {
            model.modelData.Reset(resourceManager);
        }

        LOG_TRACE(Asset, "Model unloaded: {}", model.name.c_str());
        modelIdToHandle.Remove(model.modelId);
        modelAllocator.Remove(model.selfHandle);
        model = {};
    }

    for (auto& texture : textures) {
        if (!textureAllocator.IsValid(texture.selfHandle)) { continue; }
        if (texture.refCount > 0 || texture.retireFrame == 0 || currentFrame < texture.retireFrame) { continue; }
        if (texture.loadState != Texture::LoadState::Loaded) { continue; }

        LOG_TRACE(Asset, "Texture unloaded: {} (bindless index: {})", texture.name.c_str(), static_cast<uint32_t>(texture.bindlessHandle.index));
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(texture.bindlessHandle);
        textureIdToHandle.Remove(texture.textureId);
        textureAllocator.Remove(texture.selfHandle);
        texture = {};
    }

    for (auto& sampler : samplers) {
        if (!samplerAllocator.IsValid(sampler.selfHandle)) { continue; }
        if (sampler.refCount > 0 || sampler.retireFrame == 0 || currentFrame < sampler.retireFrame) { continue; }

        LOG_TRACE(Asset, "Sampler unloaded (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index));
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseSamplerBinding(sampler.bindlessHandle);
        samplerIdToHandle.Remove(sampler.id);
        samplerAllocator.Remove(sampler.selfHandle);
        sampler = {};
    }
}

void AssetManager::Scan()
{
    bool expectedRescan = true;
    if (ctx->bShouldRescanResources.compare_exchange_strong(expectedRescan, false, std::memory_order::acq_rel, std::memory_order::relaxed)) {
        const Core::Path& assetPath = Platform::GetAssetPath();
        if (assetPath.Exists()) {
            Core::Vector<Core::Path> paths;
            paths = Core::Vector<Core::Path>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
            Platform::RecursiveDirectoryIterator(assetPath, paths);

            for (const auto& path : paths) {
                const auto ext = path.Extension();

                if (ext == ".wtexture") {
                    auto header = ReadWTextureHeader(path);
                    if (!header) { continue; }
                    TextureID id{header->textureId};
                    const Core::InlineString<128> name{header->name};
                    const StringID nameSid{name.c_str(), name.Size()};
                    if (textureNameToId.Contains(nameSid) && *textureNameToId.Find(nameSid) != id) {
                        LOG_CRITICAL(Asset, "2 Textures were mounted that contain the same name. This will cause issues for texture lookups by name. ({})", path.c_str());
                    }
                    CachedTextureMetadata& cached = textureCache[id];
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(header->name);
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    textureNameToId[nameSid] = id;
                }
                else if (ext == ".wsmesh") {
                    auto optModelHeader = ReadWStaticModelHeader(path);
                    if (!optModelHeader) { continue; }
                    if (optModelHeader->modelId == 0) {
                        LOG_WARN(Asset, "Model '{}' has no modelId. Reimport to fix", path.Stem());
                        continue;
                    }
                    ModelID id{optModelHeader->modelId};

                    const Core::InlineString<128> name{optModelHeader->name};
                    const StringID nameSid{name.c_str(), name.Size()};
                    if (modelNameToId.Contains(nameSid) && *modelNameToId.Find(nameSid) != id) {
                        LOG_CRITICAL(Asset, "2 Models were mounted that contain the same name. This will cause issues for model lookups by name. ({})", path.c_str());
                    }

                    auto optNodes = ReadWStaticModelNodes(path, *optModelHeader, memoryManager->Assets(), memoryManager->AssetsScratch());
                    if (!optNodes) { continue; }

                    CachedModelMetadata& cached = modelCache[id];
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString(name);
                    cached.nodeCount = optModelHeader->nodeCount;
                    cached.meshNodesCount = optModelHeader->meshNodeCount;
                    cached.nodes = std::move(optNodes->nodes);
                    cached.bounds = optNodes->bounds;
                    modelNameToId[nameSid] = id;
                }
                else if (ext == ".wenvmap") {
                    auto header = ReadWEnvMapHeader(path);
                    if (!header) { continue; }
                    EnvironmentMapID id{header->environmentMapId};
                    const Core::InlineString<128> name{header->name};
                    const StringID nameSid{name.c_str(), name.Size()};
                    if (cubemapNameToId.Contains(nameSid) && *cubemapNameToId.Find(nameSid) != id) {
                        LOG_CRITICAL(Asset, "2 Environment maps were mounted that contain the same name. This will cause issues for cubemap lookups by name. ({})", path.c_str());
                    }
                    CachedCubemapMetadata& cached = cubemapCache[id];
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(header->name);
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cubemapNameToId[nameSid] = id;
                }
                else if (ext == ".wscene") {
                    auto header = ReadWSceneHeader(path);
                    if (!header) { continue; }
                    StringID id{header->sceneId};
                    if (header->sceneId == 0) {
                        Core::InlineString<128> stem{path.Stem()};
                        id = StringID{stem.c_str(), stem.Size()};
                        LOG_WARN(Asset, "Scene '{}' has no sceneId, using stem-derived ID. Re-save to fix", stem.c_str());
                    }
                    CachedSceneMetadata& cached = sceneCache[id];
                    cached.source = Core::Path(path);
                    cached.sceneName = Core::InlineString<128>(header->name);
                    cached.entityCount = header->entityCount;
                }
                else if (ext == ".wprefab") {
                    auto header = ReadWPrefabHeader(path);
                    if (!header) { continue; }
                    StringID id{header->prefabId};
                    if (header->prefabId == 0) {
                        Core::InlineString<128> stem{path.Stem()};
                        id = StringID{stem.c_str(), stem.Size()};
                        LOG_WARN(Asset, "Prefab '{}' has no prefabId, using stem-derived ID. Re-save to fix", stem.c_str());
                    }
                    CachedPrefabMetadata& cached = prefabCache[id];
                    cached.source = Core::Path(path);
                    cached.prefabName = Core::InlineString<128>(header->name);
                    cached.componentCount = header->componentCount;
                }
            }
        }
    }
}

Texture* AssetManager::LoadTexture(TextureID textureId)
{
    if (!textureCache.Contains(textureId)) {
        LOG_ERROR(Asset, "Texture {:x} not found in registry", textureId.id);
        return nullptr;
    }

    TextureHandle* existingPtr = textureIdToHandle.Find(textureId);
    if (existingPtr != nullptr) {
        TextureHandle existingHandle = *existingPtr;
        if (textureAllocator.IsValid(existingHandle)) {
            Texture& texture = textures[existingHandle.index];
            texture.refCount++;
            texture.retireFrame = 0;
            LOG_TRACE(Asset, "Texture already loaded: {}, refCount: {}", texture.name.c_str(), texture.refCount);
            return &texture;
        }
        textureIdToHandle.Remove(textureId);
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
    texture.name = Core::InlineString(meta.name);
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

    LOG_TRACE(Asset, "Requesting texture load: {}", texture.name.c_str());
    assetLoadManager->RequestTextureLoad(&texture);

    return &texture;
}

void AssetManager::UnloadTexture(TextureID id)
{
    TextureHandle* handlePtr = textureIdToHandle.Find(id);
    if (handlePtr == nullptr) {
        LOG_WARN(Asset, "Attempted to unload texture not in registry");
        return;
    }

    TextureHandle handle = *handlePtr;
    if (!textureAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid texture handle");
        return;
    }

    Texture& texture = textures[handle.index];
    texture.refCount--;

    LOG_TRACE(Asset, "Texture refCount decremented: {}, refCount: {}", texture.name.c_str(), texture.refCount);

    if (texture.refCount == 0) {
        texture.retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT * 4;
    }
}

Sampler* AssetManager::LoadSampler(SamplerDesc& samplerDesc)
{
    SamplerID id{fnv1a64(reinterpret_cast<uint8_t*>(&samplerDesc), sizeof(samplerDesc))};

    SamplerHandle* existingPtr = samplerIdToHandle.Find(id);
    if (existingPtr != nullptr) {
        SamplerHandle existingHandle = *existingPtr;
        if (samplerAllocator.IsValid(existingHandle)) {
            Sampler& existing = samplers[existingHandle.index];
            existing.refCount++;
            existing.retireFrame = 0;
            LOG_TRACE(Asset, "Sampler already loaded (bindless index: {}), refCount: {}", static_cast<uint32_t>(existing.bindlessHandle.index), existing.refCount);
            return &existing;
        }
        samplerIdToHandle.Remove(id);
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

    LOG_TRACE(Asset, "Requesting sampler load (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index));
    assetLoadManager->RequestSamplerLoad(&sampler);

    return &sampler;
}

void AssetManager::UnloadSampler(SamplerDesc& desc)
{
    SamplerID id{fnv1a64(reinterpret_cast<uint8_t*>(&desc), sizeof(desc))};

    SamplerHandle* handlePtr = samplerIdToHandle.Find(id);
    if (handlePtr == nullptr) {
        LOG_WARN(Asset, "Attempted to unload sampler not in registry");
        return;
    }

    SamplerHandle handle = *handlePtr;
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

CubemapHandle AssetManager::LoadCubemap(EnvironmentMapID cubemapId)
{
    if (!cubemapId.IsValid()) {
        LOG_ERROR(Asset, "LoadCubemap called with invalid EnvironmentMapID");
        return CubemapHandle::INVALID;
    }

    if (!cubemapCache.Contains(cubemapId)) {
        LOG_ERROR(Asset, "Cubemap {:x} not found in registry", cubemapId.id);
        return CubemapHandle::INVALID;
    }

    CubemapHandle* existingPtr = cubemapIdToHandle.Find(cubemapId);
    if (existingPtr != nullptr) {
        CubemapHandle existingHandle = *existingPtr;
        if (cubemapAllocator.IsValid(existingHandle)) {
            cubemaps[existingHandle.index].refCount++;
            LOG_TRACE(Asset, "Cubemap already loaded: {}, refCount: {}", cubemaps[existingHandle.index].name.c_str(), cubemaps[existingHandle.index].refCount);
            return existingHandle;
        }
        cubemapIdToHandle.Remove(cubemapId);
    }

    CubemapHandle handle = cubemapAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate cubemap slot for: {:x}", cubemapId.id);
        return CubemapHandle::INVALID;
    }

    const CachedCubemapMetadata& meta = cubemapCache[cubemapId];
    Render::Cubemap& cubemap = cubemaps[handle.index];
    cubemap.source = meta.source;
    cubemap.name = meta.name;
    cubemap.cubemapId = cubemapId;
    cubemap.dataOffset = meta.dataOffset;
    cubemap.dataSize = meta.dataSize;
    cubemap.uncompressedSize = meta.uncompressedSize;
    cubemap.refCount = 1;
    cubemap.loadState = Render::Cubemap::LoadState::Loading;
    cubemap.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateCubemap();

    cubemapIdToHandle[cubemapId] = handle;

    LOG_TRACE(Asset, "Requesting cubemap load: {}", cubemap.name.c_str());
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

    LOG_TRACE(Asset, "Cubemap refCount decremented: {}, refCount: {}", cubemap.name.c_str(), cubemap.refCount);

    if (cubemap.refCount == 0) {
        cubemap.loadState = Render::Cubemap::LoadState::NotLoaded;
        // assetLoadThread->RequestCubemapUnload(handle, &cubemap);
        cubemapIdToHandle.Remove(cubemap.cubemapId);
    }
}
} // Engine
