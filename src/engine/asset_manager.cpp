//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include <chrono>
#include <fstream>

#include "asset-load/async_asset_load_manager.h"
#include "resources/environment_map/environment_map_format.h"
#include "resources/font/font_format.h"
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
      textureRegistry(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_TEXTURES),
      staticProceduralRegistry(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_STATIC_PROCEDURAL_TEXTURES),
      cubemapNameToId(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_CUBEMAPS),
      cubemapCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_CUBEMAPS),
      sceneCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_SCENES),
      prefabCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_PREFABS),
      fontNameToId(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_FONTS),
      fontCache(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_CACHED_FONTS)
{
#if WILL_EDITOR
    // Creates white/error if they don't exist. Also creates BRDF LUT
    Editor::CreateCriticalEngineResources(&memoryManager);
#endif

    ctx->bShouldRescanResources = true;

    Scan();

    RegisterProceduralTextures();

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

    TextureID spritePointLight = FindTextureByName("sprite_point_light");
    if (spritePointLight.IsValid()) {
        Texture* spritePointLightTex = LoadTexture(spritePointLight);
        assert(spritePointLightTex && spritePointLightTex->bindlessHandle.index == SPRITE_POINT_LIGHT_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_point_light does not exist, please generate and restart the engine");
    }

    TextureID spriteAreaLight = FindTextureByName("sprite_area_light");
    if (spriteAreaLight.IsValid()) {
        Texture* spriteAreaLightTex = LoadTexture(spriteAreaLight);
        assert(spriteAreaLightTex && spriteAreaLightTex->bindlessHandle.index == SPRITE_AREA_LIGHT_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_area_light does not exist, please generate and restart the engine");
    }

    TextureID spriteDirectionalLight = FindTextureByName("sprite_directional_light");
    if (spriteDirectionalLight.IsValid()) {
        Texture* spriteDirectionalLightTex = LoadTexture(spriteDirectionalLight);
        assert(spriteDirectionalLightTex && spriteDirectionalLightTex->bindlessHandle.index == SPRITE_DIRECTIONAL_LIGHT_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_directional_light does not exist, please generate and restart the engine");
    }

    auto roboto = FindFontByName("Roboto");
    if (roboto.IsValid()) {
        LoadFont(roboto);
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
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Model already loaded: {}, refCount: {}", model.name.c_str(), model.refCount);
            }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting model load: {}", model.name.c_str());
    }
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
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Procedural model already loaded: {}, refCount: {}", model.name.c_str(), model.refCount);
            }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting procedural model load: {}", model.name.c_str());
    }
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
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Spline model already loaded: {}, refCount: {}", splineModelId.id, model.refCount);
            }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting spline model load: {}", model.name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(&model);
    return handle;
}

StaticModelHandle AssetManager::LoadText3DModel(FontHandle fontHandle, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals)
{
    const Font* probe = GetFont(fontHandle);
    if (probe == nullptr) {
        LOG_ERROR(Asset, "LoadText3DModel called with an invalid font handle");
        return StaticModelHandle::INVALID;
    }
    const uint64_t fontIdValue = probe->fontId.id;

    uint64_t hash = fnv1a64(reinterpret_cast<const uint8_t*>(&fontIdValue), sizeof(fontIdValue));
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(text.c_str()), text.Size(), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&depth), sizeof(depth), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&flatness), sizeof(flatness), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&tracking), sizeof(tracking), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&scale), sizeof(scale), hash);
    hash = fnv1a64(reinterpret_cast<const uint8_t*>(&bSmoothNormals), sizeof(bSmoothNormals), hash);

    ModelID textModelId{hash};

    StaticModelHandle* existingPtr = modelIdToHandle.Find(textModelId);
    if (existingPtr != nullptr) {
        StaticModelHandle existingHandle = *existingPtr;
        if (modelAllocator.IsValid(existingHandle)) {
            StaticModel& model = models[existingHandle.index];
            model.refCount++;
            model.retireFrame = 0;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Text3D model already loaded: {}, refCount: {}", textModelId.id, model.refCount);
            }
            return existingHandle;
        }
        modelIdToHandle.Remove(textModelId);
    }

    StaticModelHandle handle = modelAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for 3D text");
        return StaticModelHandle::INVALID;
    }

    // The component owns the font ref; if it dies mid-generation the font's retire delay keeps the slot alive until the worker job finishes reading it.
    Text3DParams params{};
    params.fontId = fontIdValue;
    params.text = text;
    params.depth = depth;
    params.flatness = flatness;
    params.tracking = tracking;
    params.scale = scale;
    params.bSmoothNormals = bSmoothNormals;
    params.font = probe;

    static int32_t text3DCounter = 0;
    StaticModel& model = models[handle.index];
    model.selfHandle = handle;
    model.name = Core::InlineString<128>::Format("Text3D Mesh %d", text3DCounter++);
    model.modelId = textModelId;
    model.text3DParams = std::move(params);
    model.refCount = 1;
    model.modelLoadState = StaticModel::ModelLoadState::NotLoaded;

    modelIdToHandle[textModelId] = handle;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting Text3D model load: {}", model.name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(&model);
    return handle;
}

StaticModelHandle AssetManager::LoadText3DModel(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals)
{
    FontHandle fontHandle = LoadFont(fontId);
    if (!fontHandle.IsValid()) {
        LOG_ERROR(Asset, "LoadText3DModel: font {} could not be loaded", fontId.id);
        return StaticModelHandle::INVALID;
    }

    StaticModelHandle handle = LoadText3DModel(fontHandle, text, depth, flatness, tracking, scale, bSmoothNormals);
    UnloadFont(fontHandle);
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Model refCount decremented: {}, refCount: {}", model.name.c_str(), model.refCount);
    }

    if (model.refCount == 0) {
        model.retireFrame = MODEL_RETIRE_PENDING;
    }
}

void AssetManager::FreezeModel(ModelID modelId)
{
    if (!IsModelFrozen(modelId)) { frozenModelIds.PushBack(modelId); }
}

void AssetManager::UnfreezeModel(ModelID modelId)
{
    frozenModelIds.RemoveFirst(modelId);
}

bool AssetManager::IsModelFrozen(ModelID modelId) const
{
    for (const ModelID& id : frozenModelIds) {
        if (id == modelId) { return true; }
    }
    return false;
}

ResolveLoadResult AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer)
{
    ResolveLoadResult loadCounts{};
    AssetLoad::StaticModelLoadComplete complete{};
    int32_t modelsThisTick{0};
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
            complete.model->acquireFrame = ctx->currentRenderFrame;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Model load succeeded: {}", complete.model->name.c_str());
            }
            loadCounts.modelLoadedCount++;
            modelsThisTick++;
        }
        else {
            complete.model->bufferAcquireOps.Clear();
            complete.model->imageAcquireOps.Clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::FailedToLoad;
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
            proceduralComplete.model->acquireFrame = ctx->currentRenderFrame;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Procedural model generation succeeded: {}", proceduralComplete.model->name.c_str());
            }
            loadCounts.modelLoadedCount++;
            modelsThisTick++;
        }
        else {
            proceduralComplete.model->bufferAcquireOps.Clear();
            proceduralComplete.model->imageAcquireOps.Clear();
            proceduralComplete.model->modelLoadState = StaticModel::ModelLoadState::FailedToLoad;
            LOG_ERROR(Asset, "Procedural model generation failed: {}", proceduralComplete.model->name.c_str());
        }
    }

    if (modelsThisTick > 0) {
        pendingModelLogCount += modelsThisTick;
        modelLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingModelLogCount > 0 && modelsThisTick == 0 && (std::chrono::steady_clock::now() - modelLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} model(s) loaded", pendingModelLogCount);
        pendingModelLogCount = 0;
    }

    AssetLoad::TextureLoadComplete textureComplete{};
    int32_t texturesThisTick{0};
    while (assetLoadManager->TryDequeueTextureComplete(textureComplete)) {
        if (textureComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.PushBack(textureComplete.texture->acquireBarrier);
            textureComplete.texture->loadState = Texture::LoadState::Loaded;
            textureComplete.texture->acquireFrame = ctx->currentRenderFrame;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Texture load succeeded: {} (bindless index: {})", textureComplete.texture->name.c_str(), static_cast<uint32_t>(textureComplete.texture->bindlessHandle.index));
            }
            loadCounts.textureLoadedCount++;
            texturesThisTick++;
        }
        else {
            textureComplete.texture->loadState = Texture::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Texture load failed: {}", textureComplete.texture->name.c_str());
        }
    }

    if (texturesThisTick > 0) {
        pendingTextureLogCount += texturesThisTick;
        textureLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingTextureLogCount > 0 && texturesThisTick == 0 && (std::chrono::steady_clock::now() - textureLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} texture(s) loaded", pendingTextureLogCount);
        pendingTextureLogCount = 0;
    }

    AssetLoad::CubemapLoadComplete cubemapComplete{};
    int32_t cubemapsThisTick{0};
    while (assetLoadManager->TryDequeueCubemapComplete(cubemapComplete)) {
        if (cubemapComplete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.PushBack(cubemapComplete.cubemap->acquireBarrier);
            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::Loaded;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Cubemap load succeeded: {} (bindless index: {})", cubemapComplete.cubemap->name.c_str(), static_cast<uint32_t>(cubemapComplete.cubemap->bindlessHandle.index));
            }
            loadCounts.cubeLoadedCount++;
            cubemapsThisTick++;
        }
        else {
            cubemapComplete.cubemap->loadState = Render::Cubemap::LoadState::NotLoaded;
            LOG_ERROR(Asset, "Cubemap load failed: {}", cubemapComplete.cubemap->name.c_str());
        }
    }

    if (cubemapsThisTick > 0) {
        pendingCubemapLogCount += cubemapsThisTick;
        cubemapLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingCubemapLogCount > 0 && cubemapsThisTick == 0 && (std::chrono::steady_clock::now() - cubemapLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} cubemap(s) loaded", pendingCubemapLogCount);
        pendingCubemapLogCount = 0;
    }

    AssetLoad::SamplerLoadComplete samplerComplete{};
    int32_t samplersThisTick{0};
    while (assetLoadManager->TryDequeueSamplerComplete(samplerComplete)) {
        if (samplerComplete.bSuccess) {
            samplerComplete.sampler->loadState = Sampler::LoadState::Loaded;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Sampler load succeeded (bindless index: {})", static_cast<uint32_t>(samplerComplete.sampler->bindlessHandle.index));
            }
            loadCounts.samplerLoadedCount++;
            samplersThisTick++;
        }
        else {
            samplerComplete.sampler->loadState = Sampler::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Sampler load failed");
        }
    }

    if (samplersThisTick > 0) {
        pendingSamplerLogCount += samplersThisTick;
        samplerLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingSamplerLogCount > 0 && samplersThisTick == 0 && (std::chrono::steady_clock::now() - samplerLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} sampler(s) loaded", pendingSamplerLogCount);
        pendingSamplerLogCount = 0;
    }

    AssetLoad::ProceduralTextureLoadComplete proceduralTexComplete{};
    int32_t proceduralTexturesThisTick{0};
    while (assetLoadManager->TryDequeueProceduralTextureComplete(proceduralTexComplete)) {
        if (proceduralTexComplete.bSuccess) {
            proceduralTexComplete.texture->loadState = Texture::LoadState::Loaded;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Procedural texture generation succeeded (bindless index: {})", static_cast<uint32_t>(proceduralTexComplete.texture->bindlessHandle.index));
            }
            loadCounts.textureLoadedCount++;
            proceduralTexturesThisTick++;
        }
        else {
            proceduralTexComplete.texture->loadState = Texture::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Procedural texture generation failed: {:x}", proceduralTexComplete.texture->textureId.id);
        }
    }

    if (proceduralTexturesThisTick > 0) {
        pendingProceduralTextureLogCount += proceduralTexturesThisTick;
        proceduralTextureLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingProceduralTextureLogCount > 0 && proceduralTexturesThisTick == 0 && (std::chrono::steady_clock::now() - proceduralTextureLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} procedural texture(s) generated", pendingProceduralTextureLogCount);
        pendingProceduralTextureLogCount = 0;
    }

    int32_t fontsThisTick{0};
    for (auto& font : fonts) {
        if (!fontAllocator.IsValid(font.selfHandle)) { continue; }
        if (font.loadState != Font::LoadState::Loading) { continue; }

        if (font.atlasTexture.loadState == Texture::LoadState::Loaded) {
            font.loadState = Font::LoadState::Loaded;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Font loaded: {} (atlas bindless index: {})", font.name.c_str(), static_cast<uint32_t>(font.atlasTexture.bindlessHandle.index));
            }
            loadCounts.fontLoadedCount++;
            fontsThisTick++;
        }
        else if (font.atlasTexture.loadState == Texture::LoadState::FailedToLoad) {
            font.loadState = Font::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Font atlas upload failed: {}", font.name.c_str());
        }
    }

    if (fontsThisTick > 0) {
        pendingFontLogCount += fontsThisTick;
        fontLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingFontLogCount > 0 && fontsThisTick == 0 && (std::chrono::steady_clock::now() - fontLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} font(s) loaded", pendingFontLogCount);
        pendingFontLogCount = 0;
    }

    return loadCounts;
}

void AssetManager::KickOffRetires()
{
    const uint64_t currentFrame = ctx->currentRenderFrame;
    for (auto& texture : textures) {
        if (!textureAllocator.IsValid(texture.selfHandle)) { continue; }
        if (texture.refCount > 0 || texture.retireFrame != TEXTURE_RETIRE_PENDING) { continue; }
        if (texture.loadState == Texture::LoadState::Loaded || texture.loadState == Texture::LoadState::FailedToLoad) {
            texture.retireFrame = currentFrame + Core::FRAME_BUFFER_COUNT * 4;
        }
    }
    for (auto& model : models) {
        if (!modelAllocator.IsValid(model.selfHandle)) { continue; }
        if (model.refCount > 0 || model.retireFrame != MODEL_RETIRE_PENDING) { continue; }
        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded || model.modelLoadState == StaticModel::ModelLoadState::FailedToLoad) {
            model.retireFrame = currentFrame + Core::FRAME_BUFFER_COUNT * 4;
        }
    }
}

bool AssetManager::ResolveUnloads()
{
    const uint64_t currentFrame = ctx->currentRenderFrame;

    int32_t modelsUnloadedThisTick{0};
    for (auto& model : models) {
        if (!modelAllocator.IsValid(model.selfHandle)) { continue; }
        if (model.refCount > 0 || model.retireFrame == 0 || model.retireFrame == MODEL_RETIRE_PENDING || currentFrame < model.retireFrame) { continue; }

        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded) {
            model.modelData.Reset(resourceManager);
        }

        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Model unloaded: {}", model.name.c_str()); }
        StaticModelHandle* stored = modelIdToHandle.Find(model.modelId);
        // If the model in the handle map is still the same one we're unloading here.
        if (stored && *stored == model.selfHandle) {
            modelIdToHandle.Remove(model.modelId);
        }
        UnfreezeModel(model.modelId); // drained: lift any hot-reload freeze
        modelAllocator.Remove(model.selfHandle);
        model = {};
        modelsUnloadedThisTick++;
    }

    if (modelsUnloadedThisTick > 0) {
        pendingModelUnloadLogCount += modelsUnloadedThisTick;
        modelUnloadLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingModelUnloadLogCount > 0 && modelsUnloadedThisTick == 0 && (std::chrono::steady_clock::now() - modelUnloadLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} model(s) unloaded", pendingModelUnloadLogCount);
        pendingModelUnloadLogCount = 0;
    }

    int32_t texturesUnloadedThisTick{0};
    for (auto& texture : textures) {
        if (!textureAllocator.IsValid(texture.selfHandle)) { continue; }
        if (texture.refCount > 0 || texture.retireFrame == 0 || texture.retireFrame == TEXTURE_RETIRE_PENDING || currentFrame < texture.retireFrame) { continue; }

        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Texture unloaded: {} (bindless index: {})", texture.name.c_str(), static_cast<uint32_t>(texture.bindlessHandle.index));
        }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(texture.bindlessHandle);
        textureIdToHandle.Remove(texture.textureId);
        if (texture.origin == Texture::Origin::RuntimeProcedural) {
            textureRegistry.Remove(texture.textureId);
            textureNameToId.Remove(StringID{texture.name.c_str(), texture.name.Size()});
        }
        textureAllocator.Remove(texture.selfHandle);
        texture = {};
        texturesUnloadedThisTick++;
    }

    if (texturesUnloadedThisTick > 0) {
        pendingTextureUnloadLogCount += texturesUnloadedThisTick;
        textureUnloadLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingTextureUnloadLogCount > 0 && texturesUnloadedThisTick == 0 && (std::chrono::steady_clock::now() - textureUnloadLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} texture(s) unloaded", pendingTextureUnloadLogCount);
        pendingTextureUnloadLogCount = 0;
    }

    int32_t samplersUnloadedThisTick{0};
    for (auto& sampler : samplers) {
        if (!samplerAllocator.IsValid(sampler.selfHandle)) { continue; }
        if (sampler.refCount > 0 || sampler.retireFrame == 0 || currentFrame < sampler.retireFrame) { continue; }

        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Sampler unloaded (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index)); }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseSamplerBinding(sampler.bindlessHandle);
        samplerIdToHandle.Remove(sampler.id);
        samplerAllocator.Remove(sampler.selfHandle);
        sampler = {};
        samplersUnloadedThisTick++;
    }

    if (samplersUnloadedThisTick > 0) {
        pendingSamplerUnloadLogCount += samplersUnloadedThisTick;
        samplerUnloadLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingSamplerUnloadLogCount > 0 && samplersUnloadedThisTick == 0 && (std::chrono::steady_clock::now() - samplerUnloadLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} sampler(s) unloaded", pendingSamplerUnloadLogCount);
        pendingSamplerUnloadLogCount = 0;
    }

    int32_t fontsUnloadedThisTick{0};
    for (auto& font : fonts) {
        if (!fontAllocator.IsValid(font.selfHandle)) { continue; }
        if (font.refCount > 0 || font.retireFrame == 0 || currentFrame < font.retireFrame) { continue; }
        if (font.loadState != Font::LoadState::Loaded) { continue; }

        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Font unloaded: {}", font.name.c_str()); }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(font.atlasTexture.bindlessHandle);
        FontHandle* storedFont = fontIdToHandle.Find(font.fontId);
        // If the font in the handle map is still the same one we're unloading here.
        if (storedFont && *storedFont == font.selfHandle) {
            fontIdToHandle.Remove(font.fontId);
        }
        UnfreezeFont(font.fontId); // drained: lift any hot-reload freeze
        fontAllocator.Remove(font.selfHandle);
        font = {};
        fontsUnloadedThisTick++;
    }

    if (fontsUnloadedThisTick > 0) {
        pendingFontUnloadLogCount += fontsUnloadedThisTick;
        fontUnloadLastActivity = std::chrono::steady_clock::now();
    }
    if (pendingFontUnloadLogCount > 0 && fontsUnloadedThisTick == 0 && (std::chrono::steady_clock::now() - fontUnloadLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} font(s) unloaded", pendingFontUnloadLogCount);
        pendingFontUnloadLogCount = 0;
    }

    // A reclaimed model/font may have just lifted a hot-reload freeze; signal so the load resolves re-run and re-acquire.
    return modelsUnloadedThisTick > 0 || fontsUnloadedThisTick > 0;
}

void AssetManager::Scan()
{
    if (ctx->bShouldRescanResources) {
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
                    DiskTextureDesc& cached = textureRegistry[id];
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(header->name);
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cached.compressionType = header->compressionType;
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
                    cached.compressionType = header->compressionType;
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
                else if (ext == ".wsfont") {
                    auto header = ReadWFontHeader(path);
                    if (!header) { continue; }
                    FontID id{header->fontId};
                    const Core::InlineString<128> name{header->name};
                    const StringID nameSid{name.c_str(), name.Size()};
                    if (fontNameToId.Contains(nameSid) && *fontNameToId.Find(nameSid) != id) {
                        LOG_CRITICAL(Asset, "Two fonts share the same name; lookups by name will be ambiguous. ({})", path.c_str());
                    }
                    CachedFontMetadata& cached = fontCache[id];
                    cached.source = path;
                    cached.name = name;
                    cached.header = *header;
                    fontNameToId[nameSid] = id;
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

    ctx->bShouldRescanResources = false;
}

void AssetManager::RegisterProceduralTextures()
{
    auto addProceduralToRegistry = [&](Core::InlineString<128> name, StringID pipelineID, uint32_t width, uint32_t height, VkFormat format, bool mipmapped) {
        const TextureID texId{pipelineID.id};
        StaticProceduralDesc& desc = staticProceduralRegistry[texId];
        desc.pipelineId = pipelineID;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.mipmapped = mipmapped;
        desc.name = Core::InlineString<128>(name);
        textureNameToId[pipelineID] = texId;
    };

    addProceduralToRegistry(Core::InlineString<128>("yellow_texture"), "yellow_texture"_sid, 256, 256, VK_FORMAT_R8G8B8A8_UNORM, true);
    addProceduralToRegistry(Core::InlineString<128>("domain_warp"), "domain_warp"_sid, 512, 512, VK_FORMAT_R8G8B8A8_UNORM, true);
}

Texture* AssetManager::LoadTexture(TextureID textureId)
{
    if (textureRegistry.Contains(textureId)) {
        return LoadDiskTexture(textureId);
    }
    if (const StaticProceduralDesc* desc = staticProceduralRegistry.Find(textureId)) {
        return LoadProceduralTexture(desc->pipelineId, desc->width, desc->height, desc->format, desc->mipmapped, Texture::Origin::StaticProcedural, desc->name);
    }
    LOG_ERROR(Asset, "LoadTexture: TextureID {:x} not found in disk cache or static procedural registry", textureId.id);
    return nullptr;
}

Texture* AssetManager::LoadDiskTexture(TextureID textureId)
{
    if (!textureRegistry.Contains(textureId)) {
        LOG_ERROR(Asset, "LoadDiskTexture: TextureID {:x} not in disk cache", textureId.id);
        return nullptr;
    }

    TextureHandle* existingPtr = textureIdToHandle.Find(textureId);
    if (existingPtr != nullptr) {
        TextureHandle existingHandle = *existingPtr;
        if (textureAllocator.IsValid(existingHandle)) {
            Texture& texture = textures[existingHandle.index];
            texture.refCount++;
            texture.retireFrame = 0;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Texture already loaded: {}, refCount: {}", texture.name.c_str(), texture.refCount);
            }
            return &texture;
        }
        textureIdToHandle.Remove(textureId);
    }

    TextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate texture slot for {:x}", textureId.id);
        return nullptr;
    }

    const DiskTextureDesc& meta = textureRegistry[textureId];

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
    texture.compressionType = meta.compressionType;
    texture.loadState = Texture::LoadState::Loading;
    texture.refCount = 1;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    textureIdToHandle[textureId] = handle;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting texture load: {}", texture.name.c_str());
    }
    assetLoadManager->RequestTextureLoad(&texture);

    return &texture;
}

Texture* AssetManager::LoadProceduralTexture(StringID pipelineId, uint32_t width, uint32_t height, VkFormat format, bool mipmapped, Texture::Origin origin, Core::InlineString<128> displayName)
{
    TextureID textureId{pipelineId.id};

    TextureHandle* existingPtr = textureIdToHandle.Find(textureId);
    if (existingPtr != nullptr) {
        if (textureAllocator.IsValid(*existingPtr)) {
            Texture& existing = textures[existingPtr->index];
            existing.refCount++;
            existing.retireFrame = 0;
            return &existing;
        }
        textureIdToHandle.Remove(textureId);
    }

    TextureHandle handle = textureAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "LoadProceduralTexture: no texture slots available");
        return nullptr;
    }

    uint32_t mipCount = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1u : 1u;

    Core::InlineString<128> name = displayName.IsEmpty() ? displayName : Core::InlineString<128>::Format("procedural_%llu", textureId.id);
    const StringID nameSid{name.c_str(), name.Size()};

    Texture& texture = textures[handle.index];
    texture.selfHandle = handle;
    texture.textureId = textureId;
    texture.name = name;
    texture.width = width;
    texture.height = height;
    texture.mipCount = mipCount;
    texture.format = format;
    texture.loadState = Texture::LoadState::Loading;
    texture.refCount = 1;
    texture.origin = origin;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    textureIdToHandle[textureId] = handle;
    textureNameToId[nameSid] = textureId;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting procedural texture load: {:x} ({}x{}, {} mips)", textureId.id, width, height, mipCount);
    }
    assetLoadManager->RequestProceduralTextureLoad(&texture, pipelineId);

    return &texture;
}

void AssetManager::GetAllTextureInfos(Core::ArenaFixedMap<TextureID, EditorTextureInfo>& out) const
{
    for (const auto& [texId, desc] : textureRegistry) {
        out[texId] = {texId, desc.name, desc.width, desc.height, desc.mipCount};
    }
    for (const auto& [texId, desc] : staticProceduralRegistry) {
        uint32_t mipCount = desc.mipmapped ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(desc.width, desc.height))))) + 1u : 1u;
        if (const TextureHandle* lh = textureIdToHandle.Find(texId); lh && textureAllocator.IsValid(*lh)) {
            mipCount = textures[lh->index].mipCount;
        }
        out[texId] = {texId, desc.name, desc.width, desc.height, mipCount};
    }
    for (const auto& [texId, handle] : textureIdToHandle) {
        if (!textureAllocator.IsValid(handle)) { continue; }
        const Texture& tex = textures[handle.index];
        if (tex.origin == Texture::Origin::RuntimeProcedural) {
            out[texId] = {texId, tex.name, tex.width, tex.height, tex.mipCount};
        }
    }
}

bool AssetManager::ReloadTexture(TextureID textureId)
{
    if (!textureRegistry.Contains(textureId)) {
        LOG_ERROR(Asset, "Texture {:x} not found in registry", textureId.id);
        return false;
    }

    TextureHandle* existingPtr = textureIdToHandle.Find(textureId);
    if (existingPtr == nullptr) {
        LOG_ERROR(Asset, "Texture {:x} requested reload but it is not currently loaded", textureId.id);
        return false;
    }

    const DiskTextureDesc& meta = textureRegistry[textureId];

    Texture& texture = textures[existingPtr->index];
    texture.selfHandle = *existingPtr;
    texture.source = meta.source;
    texture.textureId = textureId;
    texture.name = Core::InlineString(meta.name);
    texture.width = meta.width;
    texture.height = meta.height;
    texture.mipCount = meta.mipCount;
    texture.dataOffset = meta.dataOffset;
    texture.dataSize = meta.dataSize;
    texture.uncompressedSize = meta.uncompressedSize;
    texture.compressionType = meta.compressionType;
    texture.loadState = Texture::LoadState::Loading;
    texture.refCount = 1;
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting texture reload: {}", texture.name.c_str());
    }
    assetLoadManager->RequestTextureLoad(&texture);

    return true;
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Texture refCount decremented: {}, refCount: {}", texture.name.c_str(), texture.refCount);
    }

    if (texture.refCount == 0) {
        texture.retireFrame = TEXTURE_RETIRE_PENDING;
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
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Sampler already loaded (bindless index: {}), refCount: {}", static_cast<uint32_t>(existing.bindlessHandle.index), existing.refCount);
            }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting sampler load (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index));
    }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Sampler refCount decremented: {}, refCount: {}", sampler.id.id, sampler.refCount);
    }

    if (sampler.refCount == 0) {
        sampler.retireFrame = ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4;
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
    cubemap.compressionType = meta.compressionType;
    cubemap.refCount = 1;
    cubemap.loadState = Render::Cubemap::LoadState::Loading;
    cubemap.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateCubemap();

    cubemapIdToHandle[cubemapId] = handle;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting cubemap load: {}", cubemap.name.c_str());
    }
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

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Cubemap refCount decremented: {}, refCount: {}", cubemap.name.c_str(), cubemap.refCount);
    }

    if (cubemap.refCount == 0) {
        cubemap.loadState = Render::Cubemap::LoadState::NotLoaded;
        // assetLoadThread->RequestCubemapUnload(handle, &cubemap);
        cubemapIdToHandle.Remove(cubemap.cubemapId);
    }
}

FontHandle AssetManager::LoadFont(FontID id)
{
    if (!fontCache.Contains(id)) {
        LOG_ERROR(Asset, "Font {:x} not found in registry", id.id);
        return FontHandle::INVALID;
    }

    FontHandle* existingPtr = fontIdToHandle.Find(id);
    if (existingPtr != nullptr) {
        FontHandle existingHandle = *existingPtr;
        if (fontAllocator.IsValid(existingHandle)) {
            Font& font = fonts[existingHandle.index];
            font.refCount++;
            font.retireFrame = 0;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Font already loaded: {}, refCount: {}", font.name.c_str(), font.refCount);
            }
            return existingHandle;
        }
        fontIdToHandle.Remove(id);
    }

    FontHandle handle = fontAllocator.Add();
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate font slot for {:x}", id.id);
        return FontHandle::INVALID;
    }

    const CachedFontMetadata& meta = fontCache[id];
    Font& font = fonts[handle.index];
    font.selfHandle = handle;
    font.fontId = id;
    font.name = meta.name;
    font.source = meta.source;
    font.header = meta.header;
    font.refCount = 1;
    font.loadState = Font::LoadState::Loading;

    //
    {
        std::ifstream f(meta.source.c_str(), std::ios::binary);
        if (!f) {
            LOG_ERROR(Asset, "Failed to open font file: {}", meta.source.c_str());
            fontAllocator.Remove(handle);
            font = {};
            return FontHandle::INVALID;
        }
        font.glyphs = Core::HeapArray<WGlyphInfo>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta.header.glyphCount);
        f.seekg(static_cast<std::streamoff>(meta.header.glyphDataOffset));
        f.read(reinterpret_cast<char*>(font.glyphs.Data()), static_cast<std::streamsize>(meta.header.glyphCount * sizeof(WGlyphInfo)));
        if (!f) {
            LOG_ERROR(Asset, "Failed to read glyph data for font: {}", meta.name.c_str());
            fontAllocator.Remove(handle);
            font = {};
            return FontHandle::INVALID;
        }

        // 3D Text
        if (meta.header.contourGlyphCount != 0) {
            font.glyphContourRanges = Core::HeapArray<WGlyphContourRange>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta.header.contourGlyphCount);
            font.contourRanges = Core::HeapArray<WContourRange>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta.header.contourCount);
            font.contourEdges = Core::HeapArray<WFontEdge>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta.header.edgeCount);
            f.seekg(static_cast<std::streamoff>(meta.header.glyphContourRangeOffset));
            f.read(reinterpret_cast<char*>(font.glyphContourRanges.Data()), static_cast<std::streamsize>(meta.header.contourGlyphCount * sizeof(WGlyphContourRange)));
            f.read(reinterpret_cast<char*>(font.contourRanges.Data()), static_cast<std::streamsize>(meta.header.contourCount * sizeof(WContourRange)));
            f.read(reinterpret_cast<char*>(font.contourEdges.Data()), static_cast<std::streamsize>(meta.header.edgeCount * sizeof(WFontEdge)));
            if (!f) {
                LOG_ERROR(Asset, "Failed to read contour data for font: {}", meta.name.c_str());
                fontAllocator.Remove(handle);
                font = {};
                return FontHandle::INVALID;
            }
        }
    }

    font.atlasTexture.source = meta.source;
    font.atlasTexture.name = meta.name;
    font.atlasTexture.width = meta.header.atlasWidth;
    font.atlasTexture.height = meta.header.atlasHeight;
    font.atlasTexture.mipCount = 1;
    font.atlasTexture.dataOffset = meta.header.atlasDataOffset;
    font.atlasTexture.dataSize = meta.header.atlasDataSize;
    font.atlasTexture.uncompressedSize = meta.header.atlasUncompressedSize;
    font.atlasTexture.compressionType = meta.header.atlasCompressionType;
    font.atlasTexture.loadState = Texture::LoadState::Loading;
    font.atlasTexture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    fontIdToHandle[id] = handle;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting font load: {}", font.name.c_str());
    }
    assetLoadManager->RequestTextureLoad(&font.atlasTexture);

    return handle;
}

void AssetManager::UnloadFont(FontHandle handle)
{
    if (!fontAllocator.IsValid(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid font handle");
        return;
    }

    Font& font = fonts[handle.index];
    font.refCount--;

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Font refCount decremented: {}, refCount: {}", font.name.c_str(), font.refCount);
    }

    if (font.refCount == 0) {
        font.retireFrame = ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4;
    }
}

void AssetManager::FreezeFont(FontID fontId)
{
    if (!IsFontFrozen(fontId)) { frozenFontIds.PushBack(fontId); }
}

void AssetManager::UnfreezeFont(FontID fontId)
{
    frozenFontIds.RemoveFirst(fontId);
}

bool AssetManager::IsFontFrozen(FontID fontId) const
{
    for (const FontID& id : frozenFontIds) {
        if (id == fontId) { return true; }
    }
    return false;
}

Font* AssetManager::GetFont(FontHandle handle)
{
    if (!fontAllocator.IsValid(handle)) {
        return nullptr;
    }
    return &fonts[handle.index];
}

const WGlyphInfo* AssetManager::GetGlyph(FontHandle handle, uint32_t codepoint) const
{
    if (!fontAllocator.IsValid(handle)) {
        return nullptr;
    }

    const Font& font = fonts[handle.index];
    if (font.loadState != Font::LoadState::Loaded) {
        return nullptr;
    }

    for (uint32_t i = 0; i < font.glyphs.Size(); ++i) {
        if (font.glyphs[i].codepoint == codepoint) {
            return &font.glyphs[i];
        }
    }
    return nullptr;
}
} // Engine
