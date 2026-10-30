//
// Created by William on 2025-12-22.
//

#include "asset_manager.h"

#include <algorithm>
#include <chrono>

#include "asset-load/async_asset_load_manager.h"
#include "resources/environment_map/environment_map_format.h"
#include "resources/environment_map/probe_format.h"
#include "resources/font/font_format.h"
#include "resources/prefab/prefab_format.h"
#include "resources/scene/scene_format.h"
#include "resources/scene/play_format.h"
#include "editor/asset-generation/miscellaneous_asset_generate.h"
#include "logging/engine_log.h"
#include "platform/file_utils.h"
#include "platform/paths.h"
#include "render/resource_manager.h"
#include "resources/model/model_format.h"
#include "resources/physics/collider_generation.h"

namespace Engine
{
AssetManager::AssetManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager)
    : memoryManager(&memoryManager), ctx(ctx), assetLoadManager(assetLoadManager), resourceManager(resourceManager),
      changedModelIds(&memoryManager.Assets(), Core::AllocTag::AssetManager),
      changedTextureIds(&memoryManager.Assets(), Core::AllocTag::AssetManager),
      changedFontIds(&memoryManager.Assets(), Core::AllocTag::AssetManager),
      changedEnvironmentMapIds(&memoryManager.Assets(), Core::AllocTag::AssetManager),
      changedTextureArrayIds(&memoryManager.Assets(), Core::AllocTag::AssetManager),
      deferredTextureBindingReleases(&memoryManager.Assets(), Core::AllocTag::AssetManager, MAX_LOADED_TEXTURES),
      deferredCubemapBindingReleases(&memoryManager.Assets(), Core::AllocTag::AssetManager, MAX_LOADED_CUBEMAPS),
      deferredTextureArrayReleases(&memoryManager.Assets(), Core::AllocTag::AssetManager, MAX_LOADED_TEXTURE_ARRAYS)
{
    Core::VirtualMemoryManager& vm = memoryManager.Virtual();
    models.Init(&vm, Core::AllocTag::AssetManager, "AssetModels");
    moduleParamsPool.Init(&vm, Core::AllocTag::AssetManager, "AssetModuleParams");
    splineParamsPool.Init(&vm, Core::AllocTag::AssetManager, "AssetSplineParams");
    text3DParamsPool.Init(&vm, Core::AllocTag::AssetManager, "AssetText3DParams");
    colliders.Init(&vm, Core::AllocTag::AssetManager, "AssetColliders");
    textures.Init(&vm, Core::AllocTag::AssetManager, "AssetTextures");
    samplers.Init(&vm, Core::AllocTag::AssetManager, "AssetSamplers");
    cubemaps.Init(&vm, Core::AllocTag::AssetManager, "AssetCubemaps");
    textureArrays.Init(&vm, Core::AllocTag::AssetManager, "AssetTextureArrays");
    fonts.Init(&vm, Core::AllocTag::AssetManager, "AssetFonts");

    Core::TlsfAllocator* assets = &memoryManager.Assets();
    modelCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_MODELS, "Model");
    textureRegistry.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_TEXTURES, "Texture");
    cubemapCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_CUBEMAPS, "Cubemap");
    probeRegistry.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_PROBES, "Probe");
    textureArrayCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_TEXTURE_ARRAYS, "Texture array");
    shadowMapRegistry.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_TEXTURE_ARRAYS, "Shadow map");
    fontCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_FONTS, "Font");
    sceneCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_SCENES, "Scene");
    prefabCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_PREFABS, "Prefab");
    playCache.Init(&vm, assets, Core::AllocTag::AssetManager, MAX_CACHED_PLAYS, "Run");

#if WILL_EDITOR
    // Creates white/error if they don't exist. Also creates BRDF LUT
    Editor::CreateCriticalEngineResources(&memoryManager);
#endif

    ctx->rescan.bResources = true;

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

    TextureID blueNoise = FindTextureByName("blue_noise");
    if (blueNoise.IsValid()) {
        Texture* blueNoiseTex = LoadTexture(blueNoise);
        assert(blueNoiseTex && blueNoiseTex->bindlessHandle.index == BLUE_NOISE_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Blue noise texture does not exist, please regenerate and restart the engine");
    }

    TextureID spriteReflectionProbe = FindTextureByName("sprite_reflection_probe");
    if (spriteReflectionProbe.IsValid()) {
        Texture* spriteReflectionProbeTex = LoadTexture(spriteReflectionProbe);
        assert(spriteReflectionProbeTex && spriteReflectionProbeTex->bindlessHandle.index == SPRITE_REFLECTION_PROBE_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_reflection_probe does not exist, please generate and restart the engine");
    }

    TextureID spriteDDGIVolume = FindTextureByName("sprite_ddgi_volume");
    if (spriteDDGIVolume.IsValid()) {
        Texture* spriteDDGIVolumeTex = LoadTexture(spriteDDGIVolume);
        assert(spriteDDGIVolumeTex && spriteDDGIVolumeTex->bindlessHandle.index == SPRITE_DDGI_VOLUME_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_ddgi_volume does not exist, please generate and restart the engine");
    }

    TextureID spritePostProcessVolume = FindTextureByName("sprite_post_process_volume");
    if (spritePostProcessVolume.IsValid()) {
        Texture* spritePostProcessVolumeTex = LoadTexture(spritePostProcessVolume);
        assert(spritePostProcessVolumeTex && spritePostProcessVolumeTex->bindlessHandle.index == SPRITE_POST_PROCESS_VOLUME_BINDLESS_INDEX);
    }
    else {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        LOG_CRITICAL(Asset, "Sprite texture sprite_post_process_volume does not exist, please generate and restart the engine");
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
    models.ForEach([this](StaticModelHandle, StaticModel& model) {
        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded) {
            model.modelData.Reset(resourceManager);
        }
    });
}

const IAssetLoader::CachedSceneMetadata* AssetManager::GetSceneMetadata(StringID sceneId) const
{
    return sceneCache.Find(sceneId);
}

void AssetManager::RegisterScene(StringID sceneId, const Core::Path& source)
{
    CachedSceneMetadata& cached = sceneCache.Stamp(sceneId, {}, scanGeneration, source).entry;
    cached.source = source;
    cached.sceneName = Core::InlineString<128>(source.Stem());
    cached.entityCount = 0;
    cached.bUnsaved = true;
}

void AssetManager::UpdateSceneCachePath(StringID sceneId, const Core::Path& path, uint32_t entityCount)
{
    auto it = sceneCache.Find(sceneId);
    if (!it) {
        return;
    }
    it->source = path;
    it->entityCount = entityCount;
    it->bUnsaved = false;
}

const IAssetLoader::CachedPrefabMetadata* AssetManager::GetPrefabMetadata(StringID prefabId) const
{
    return prefabCache.Find(prefabId);
}

bool AssetManager::DeleteScene(StringID sceneId)
{
    const CachedSceneMetadata* it = sceneCache.Find(sceneId);
    if (!it) { return false; }
    if (!it->bUnsaved && !Platform::MoveToRecycleBin(it->source)) {
        LOG_ERROR(Asset, "Failed to delete scene '{}'", it->source.c_str());
        return false;
    }
    for (const auto& [id, run] : playCache) {
        if (run.sceneId == sceneId && !Platform::MoveToRecycleBin(run.source)) {
            LOG_WARN(Asset, "Failed to delete run '{}'", run.source.c_str());
        }
    }
    if (const Core::Path runtimePath = RuntimeScenePath(it->source); Platform::FileExists(runtimePath) && !Platform::MoveToRecycleBin(runtimePath)) {
        LOG_WARN(Asset, "Failed to delete runtime scene '{}'", runtimePath.c_str());
    }
    sceneCache.Remove(sceneId);
    ctx->rescan.bScenes = true;
    return true;
}

bool AssetManager::MoveScene(StringID sceneId, const Core::Path& newSource)
{
    CachedSceneMetadata* meta = sceneCache.Find(sceneId);
    if (!meta || Platform::FileExists(newSource)) { return false; }

    const Core::Path oldSource = meta->source;
    if (!meta->bUnsaved) {
        Platform::CreateDirectories(newSource.Parent());
        if (!Platform::RenameFile(oldSource, newSource)) {
            LOG_ERROR(Asset, "Failed to move scene '{}' to '{}'", oldSource.c_str(), newSource.c_str());
            return false;
        }
        const Core::Path oldRuntime = RuntimeScenePath(oldSource);
        const Core::Path newRuntime = RuntimeScenePath(newSource);
        if (Platform::FileExists(oldRuntime) && (Platform::FileExists(newRuntime) || !Platform::RenameFile(oldRuntime, newRuntime))) {
            LOG_WARN(Asset, "Runtime scene '{}' was not moved to '{}'", oldRuntime.c_str(), newRuntime.c_str());
        }
    }
    meta->source = newSource;
    meta->sceneName = Core::InlineString<128>(newSource.Stem());

    const Core::Path oldFolder = oldSource.Parent();
    const Core::Path newFolder = newSource.Parent();
    if (oldFolder != newFolder) {
        for (const auto& [id, run] : playCache) {
            if (run.sceneId != sceneId || run.source.Parent() != oldFolder) { continue; }
            const Core::Path runTarget = newFolder / run.source.Filename();
            if (Platform::FileExists(runTarget) || !Platform::RenameFile(run.source, runTarget)) {
                LOG_WARN(Asset, "Run '{}' stayed in '{}'", run.source.c_str(), oldFolder.c_str());
            }
        }
    }

    ctx->rescan.bScenes = true;
    return true;
}

bool AssetManager::DeletePrefab(StringID prefabId)
{
    const CachedPrefabMetadata* it = prefabCache.Find(prefabId);
    if (!it) { return false; }
    if (Platform::FileExists(it->source) && !Platform::MoveToRecycleBin(it->source)) {
        LOG_ERROR(Asset, "Failed to delete prefab '{}'", it->source.c_str());
        return false;
    }
    prefabCache.Remove(prefabId);
    return true;
}

StaticModelHandle AssetManager::LoadModel(ModelID modelId)
{
    const CachedModelMetadata* meta = modelCache.Find(modelId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Model '{}' not found in registry", modelId.id);
        return StaticModelHandle::INVALID;
    }

    const auto [handle, model, bCreated] = models.Acquire(modelId, [&](StaticModel& m, StaticModelHandle h) {
        m.selfHandle = h;
        m.name = meta->name;
        m.source = meta->source;
        m.uncompressedBodySize = meta->uncompressedBodySize;
        m.modelId = modelId;
        m.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", meta->name.c_str());
        return StaticModelHandle::INVALID;
    }
    if (!bCreated) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Model already loaded: {}, refCount: {}", model->name.c_str(), models.RefCount(handle));
        }
        return handle;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting model load: {}", model->name.c_str());
    }
    assetLoadManager->RequestModelLoad(model);

    return handle;
}

StaticModelHandle AssetManager::LoadProceduralModel(const ProceduralParams& params, const ProceduralRepeat& repeat)
{
    HashBuilder h;
    h.Add(params.index());
    std::visit([&h](const auto& v) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, std::monostate>) {
            h.Add(&v, sizeof(v));
        }
    }, params);
    if (repeat.IsActive()) {
        h.Add(&repeat, sizeof(repeat));
    }

    const ModelID proceduralModelId{h.Finish()};

    static std::atomic<int32_t> proceduralCounter{0};
    const auto [handle, model, bCreated] = models.Acquire(proceduralModelId, [&](StaticModel& m, StaticModelHandle mh) {
        m.selfHandle = mh;
        m.name = Core::InlineString<128>::Format("Procedural Mesh %d", proceduralCounter.fetch_add(1, std::memory_order_relaxed));
        m.modelId = proceduralModelId;
        m.proceduralParams = params;
        m.proceduralRepeat = repeat.IsActive() ? repeat : ProceduralRepeat{};
        m.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for: {}", proceduralModelId.id);
        return StaticModelHandle::INVALID;
    }
    if (!bCreated) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Procedural model already loaded: {}, refCount: {}", model->name.c_str(), models.RefCount(handle));
        }
        return handle;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting procedural model load: {}", model->name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(model);

    return handle;
}

static uint64_t HashSplineParams(const SplineParams& params)
{
    HashBuilder h;
    h.Add(params.spline.points.Data(), params.spline.points.Size() * sizeof(SplinePoint));
    h.Add(params.spline.mode);
    h.Add(params.spline.bClosed);
    h.Add(params.radius);
    h.Add(params.rollAngle);
    h.Add(params.sides);
    h.Add(params.segmentsPerSpan);
    h.Add(params.bCaps);
    h.Add(params.bCrossPlanks);
    h.Add(params.crossPlankInterval);
    h.Add(params.crossPlankHeight);
    h.Add(params.crossPlankThickness);
    h.Add(params.crossPlankLength);
    h.Add(params.tubeThickness);
    h.Add(params.bFacetNormals);
    h.Add(params.profile.type);
    h.Add(params.profile.width);
    h.Add(params.profile.height);
    h.Add(params.profile.cornerRadius);
    h.Add(params.profile.cornerSegments);
    h.Add(params.profile.thickness);
    h.Add(params.profile.wallChamfer);
    h.Add(params.profile.wallChamferEdge);
    h.Add(params.railing.bEnabled);
    h.Add(params.railing.bPosts);
    h.Add(params.railing.postInterval);
    h.Add(params.railing.postBottom);
    h.Add(params.railing.postTop);
    h.Add(params.railing.postSize);
    h.Add(params.railing.postLateral);
    h.Add(params.railing.lateralOffset);
    h.Add(params.railing.postShape);
    h.Add(params.railing.postSides);
    h.Add(params.railing.postsPerInterval);
    h.Add(params.railing.lanes.Data(), params.railing.lanes.Size() * sizeof(Vec2));
    for (size_t i = 0; i < params.railing.lanes.Size(); i++) { h.Add(params.railing.LaneScale(i)); }
    return h.Finish();
}

StaticModelHandle AssetManager::LoadSplineModel(const SplineParams& params)
{
    const ModelID splineModelId{HashSplineParams(params)};

    if (const StaticModelHandle existing = models.AddRef(splineModelId); existing.IsValid()) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Spline model already loaded: {}, refCount: {}", splineModelId.id, models.RefCount(existing));
        }
        return existing;
    }

    const auto paramsHandle = splineParamsPool.Add();
    SplineParams* pooledParams = splineParamsPool.Get(paramsHandle);
    if (pooledParams == nullptr) {
        LOG_ERROR(Asset, "Spline params pool exhausted ({} live spline models/colliders)", MAX_POOLED_SPLINE_PARAMS);
        return StaticModelHandle::INVALID;
    }
    *pooledParams = params;

    static std::atomic<int32_t> splineCounter{0};
    const auto [handle, model, bCreated] = models.Acquire(splineModelId, [&](StaticModel& m, StaticModelHandle h) {
        m.selfHandle = h;
        m.name = Core::InlineString<128>::Format("Spline Mesh %d", splineCounter.fetch_add(1, std::memory_order_relaxed));
        m.modelId = splineModelId;
        m.splineParams = pooledParams;
        m.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
    });
    if (!bCreated) { splineParamsPool.Remove(paramsHandle); }
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for spline");
        return StaticModelHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting spline model load: {}", model->name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(model);
    return handle;
}

StaticModelHandle AssetManager::LoadModuleModel(const ModuleParams& params)
{
    constexpr uint8_t kind = 77;
    HashBuilder h;
    h.Add(kind);
    h.Add(params.parts.Size());
    for (const ModulePart& part : params.parts) {
        h.Add(part.shape.index());
        std::visit([&h]<typename T0>(const T0& v) {
            if constexpr (!std::is_same_v<std::decay_t<T0>, std::monostate>) {
                h.Add(&v, sizeof(v));
            }
        }, part.shape);
        h.Add(&part.offset, sizeof(part.offset));
        h.Add(&part.rotation, sizeof(part.rotation));
        h.Add(part.materialSlot);
    }

    const ModelID moduleModelId{h.Finish()};

    if (const StaticModelHandle existing = models.AddRef(moduleModelId); existing.IsValid()) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Module model already loaded: {}, refCount: {}", moduleModelId.id, models.RefCount(existing));
        }
        return existing;
    }

    const auto paramsHandle = moduleParamsPool.Add();
    ModuleParams* pooledParams = moduleParamsPool.Get(paramsHandle);
    if (pooledParams == nullptr) {
        LOG_ERROR(Asset, "Module params pool exhausted ({} live module models)", MAX_LOADED_MODULE_MODELS);
        return StaticModelHandle::INVALID;
    }
    *pooledParams = params;

    static std::atomic<int32_t> moduleCounter{0};
    const auto [handle, model, bCreated] = models.Acquire(moduleModelId, [&](StaticModel& m, StaticModelHandle mh) {
        m.selfHandle = mh;
        m.name = Core::InlineString<128>::Format("Module Mesh %d", moduleCounter.fetch_add(1, std::memory_order_relaxed));
        m.modelId = moduleModelId;
        m.moduleParams = pooledParams;
        m.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
    });
    if (!bCreated) { moduleParamsPool.Remove(paramsHandle); }
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for module");
        return StaticModelHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting module model load: {}", model->name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(model);
    return handle;
}

StaticModelHandle AssetManager::LoadText3DModel(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius)
{
    assert(!IsFontFrozen(fontId) && "LoadText3DModel called with a frozen font. You should freeze-gate (IsFontFrozen) before generating");

    HashBuilder h;
    h.Add(fontId.id);
    h.Add(text.c_str(), text.Size());
    h.Add(depth);
    h.Add(flatness);
    h.Add(tracking);
    h.Add(scale);
    h.Add(bSmoothNormals);
    h.Add(static_cast<uint8_t>(align));
    h.Add(static_cast<uint8_t>(anchor));
    if (wrapWidth > 0.0f) {
        h.Add(wrapWidth);
    }
    if (bendRadius != 0.0f) {
        h.Add(bendRadius);
    }

    const ModelID textModelId{h.Finish()};

    if (const StaticModelHandle existing = models.AddRef(textModelId); existing.IsValid()) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Text3D model already loaded: {}, refCount: {}", textModelId.id, models.RefCount(existing));
        }
        return existing;
    }

    const auto paramsHandle = text3DParamsPool.Add();
    Text3DParams* pooledParams = text3DParamsPool.Get(paramsHandle);
    if (pooledParams == nullptr) {
        LOG_ERROR(Asset, "Text3D params pool exhausted ({} live text3D models/colliders)", MAX_POOLED_TEXT3D_PARAMS);
        return StaticModelHandle::INVALID;
    }

    FontHandle fontHandle = LoadFont(fontId);
    if (!fontHandle.IsValid()) {
        LOG_ERROR(Asset, "LoadText3DModel: font {} could not be loaded", fontId.id);
        text3DParamsPool.Remove(paramsHandle);
        return StaticModelHandle::INVALID;
    }

    Text3DParams& params = *pooledParams;
    params.fontId = fontId.id;
    params.text = text;
    params.depth = depth;
    params.flatness = flatness;
    params.tracking = tracking;
    params.scale = scale;
    params.wrapWidth = wrapWidth;
    params.bendRadius = bendRadius;
    params.bSmoothNormals = bSmoothNormals;
    params.align = align;
    params.anchor = anchor;
    params.font = GetFont(fontHandle);

    static std::atomic<int32_t> text3DCounter{0};
    const auto [handle, model, bCreated] = models.Acquire(textModelId, [&](StaticModel& m, StaticModelHandle mh) {
        m.selfHandle = mh;
        m.name = Core::InlineString<128>::Format("Text3D Mesh %d", text3DCounter.fetch_add(1, std::memory_order_relaxed));
        m.modelId = textModelId;
        m.text3DParams = pooledParams;
        // Generation-scoped font ref: hand the model the ref we took so the worker can read the glyph contours; it keeps the font resident until the model finalizes (released in ResolveLoads).
        m.text3DFontHandle = fontHandle;
        m.modelLoadState = StaticModel::ModelLoadState::NotLoaded;
    });
    if (!bCreated) {
        text3DParamsPool.Remove(paramsHandle);
        UnloadFont(fontHandle);
    }
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate model slot for 3D text");
        return StaticModelHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting Text3D model load: {}", model->name.c_str());
    }
    assetLoadManager->RequestProceduralModelLoad(model);
    return handle;
}

StaticModel* AssetManager::GetModel(StaticModelHandle handle)
{
    return models.Get(handle);
}

const StaticModel* AssetManager::GetModel(StaticModelHandle handle) const
{
    return models.Get(handle);
}

void AssetManager::UnloadModel(StaticModelHandle handle)
{
    if (!models.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid model handle");
        return;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Model refCount decremented: {}, refCount: {}", models.Get(handle)->name.c_str(), models.RefCount(handle));
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

PhysicsColliderHandle AssetManager::LoadSplineCollider(const SplineParams& params, bool bHollow)
{
    const bool bHollowMesh = bHollow && CanBuildHollowSplineCollider(params);
    const PhysicsColliderKind kind = bHollowMesh ? PhysicsColliderKind::TriangleMesh : PhysicsColliderKind::Compound;
    HashBuilder h;
    h.Add(HashSplineParams(params));
    h.Add(static_cast<uint8_t>(kind));

    const PhysicsColliderID colliderId{h.Finish()};

    if (const PhysicsColliderHandle existing = colliders.AddRef(colliderId); existing.IsValid()) {
        return existing;
    }

    const auto paramsHandle = splineParamsPool.Add();
    SplineParams* pooledParams = splineParamsPool.Get(paramsHandle);
    if (pooledParams == nullptr) {
        LOG_ERROR(Asset, "Spline params pool exhausted ({} live spline models/colliders)", MAX_POOLED_SPLINE_PARAMS);
        return PhysicsColliderHandle::INVALID;
    }
    *pooledParams = params;

    static std::atomic<int32_t> splineColliderCounter{0};
    const auto [handle, collider, bCreated] = colliders.Acquire(colliderId, [&](PhysicsColliderAsset& c, PhysicsColliderHandle ch) {
        c.selfHandle = ch;
        c.name = Core::InlineString<128>::Format("Spline Collider %d", splineColliderCounter.fetch_add(1, std::memory_order_relaxed));
        c.colliderId = colliderId;
        c.kind = kind;
        c.splineParams = pooledParams;
        c.bSplineHollow = bHollowMesh;
        c.loadState = PhysicsColliderAsset::LoadState::NotLoaded;
    });
    if (!bCreated) { splineParamsPool.Remove(paramsHandle); }
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate collider slot for spline");
        return PhysicsColliderHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    assetLoadManager->RequestPhysicsColliderLoad(collider);
    return handle;
}

PhysicsColliderHandle AssetManager::LoadProceduralCollider(const ProceduralParams& params, const ProceduralRepeat& repeat)
{
    HashBuilder h;
    h.Add(params.index());
    std::visit([&h]<typename T0>(const T0& v) {
        if constexpr (!std::is_same_v<std::decay_t<T0>, std::monostate>) {
            h.Add(&v, sizeof(v));
        }
    }, params);
    if (repeat.IsActive()) {
        h.Add(&repeat, sizeof(repeat));
    }
    constexpr uint8_t domain = 1;
    h.Add(domain);

    const PhysicsColliderID colliderId{h.Finish()};

    static std::atomic<int32_t> proceduralColliderCounter{0};
    const auto [handle, collider, bCreated] = colliders.Acquire(colliderId, [&](PhysicsColliderAsset& c, PhysicsColliderHandle ch) {
        c.selfHandle = ch;
        c.name = Core::InlineString<128>::Format("Procedural Collider %d", proceduralColliderCounter.fetch_add(1, std::memory_order_relaxed));
        c.colliderId = colliderId;
        c.proceduralParams = params;
        c.proceduralRepeat = repeat.IsActive() ? repeat : ProceduralRepeat{};

        // Analytics are either Compound or Convex Hull (determined in the load slot). Exotics are concave and must use triangle mesh (not allowed to be dynamic)
        const bool bAnalytic = CanBuildProceduralCollider(params);
        c.kind = bAnalytic ? PhysicsColliderKind::Compound : PhysicsColliderKind::TriangleMesh;
        c.loadState = PhysicsColliderAsset::LoadState::NotLoaded;
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate collider slot for procedural shape");
        return PhysicsColliderHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    assetLoadManager->RequestPhysicsColliderLoad(collider);
    return handle;
}

PhysicsColliderHandle AssetManager::LoadModelCollider(Engine::ModelID sourceModelId, PhysicsColliderKind kind, uint32_t nodeIndex)
{
    const CachedModelMetadata* meta = modelCache.Find(sourceModelId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Model '{}' not found in registry for collider", sourceModelId.id);
        return PhysicsColliderHandle::INVALID;
    }
    if (nodeIndex != ~0u && nodeIndex >= meta->nodes.Size()) {
        nodeIndex = ~0u;
    }

    HashBuilder h;
    h.Add(sourceModelId.id);
    h.Add(static_cast<uint8_t>(kind));
    constexpr uint8_t domain = 2;
    h.Add(domain);
    if (nodeIndex != ~0u) {
        h.Add(nodeIndex);
    }

    const PhysicsColliderID colliderId{h.Finish()};

    static std::atomic<int32_t> modelColliderCounter{0};
    const auto [handle, collider, bCreated] = colliders.Acquire(colliderId, [&](PhysicsColliderAsset& c, PhysicsColliderHandle ch) {
        c.selfHandle = ch;
        c.name = Core::InlineString<128>::Format("Model Collider %d", modelColliderCounter.fetch_add(1, std::memory_order_relaxed));
        c.colliderId = colliderId;
        c.kind = kind;
        c.sourceModelId = sourceModelId;
        c.sourceNodeIndex = nodeIndex;
        c.source = meta->source;
        c.loadState = PhysicsColliderAsset::LoadState::NotLoaded;
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate collider slot for model");
        return PhysicsColliderHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    assetLoadManager->RequestPhysicsColliderLoad(collider);
    return handle;
}

PhysicsColliderHandle AssetManager::LoadText3DCollider(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius, bool bPrecise)
{
    assert(!IsFontFrozen(fontId) && "LoadText3DCollider called with a frozen font. Freeze-gate (IsFontFrozen) before generating");

    HashBuilder h;
    h.Add(fontId.id);
    h.Add(text.c_str(), text.Size());
    h.Add(depth);
    h.Add(flatness);
    h.Add(tracking);
    h.Add(scale);
    h.Add(bSmoothNormals);
    h.Add(static_cast<uint8_t>(align));
    h.Add(static_cast<uint8_t>(anchor));
    if (wrapWidth > 0.0f) {
        h.Add(wrapWidth);
    }
    if (bendRadius != 0.0f) {
        h.Add(bendRadius);
    }
    h.Add(bPrecise);
    const PhysicsColliderKind kind = bPrecise ? PhysicsColliderKind::TriangleMesh : PhysicsColliderKind::Compound;
    h.Add(static_cast<uint8_t>(kind));
    constexpr uint8_t domain = 3;
    h.Add(domain);

    const PhysicsColliderID colliderId{h.Finish()};

    if (const PhysicsColliderHandle existing = colliders.AddRef(colliderId); existing.IsValid()) {
        return existing;
    }

    const auto paramsHandle = text3DParamsPool.Add();
    Text3DParams* pooledParams = text3DParamsPool.Get(paramsHandle);
    if (pooledParams == nullptr) {
        LOG_ERROR(Asset, "Text3D params pool exhausted ({} live text3D models/colliders)", MAX_POOLED_TEXT3D_PARAMS);
        return PhysicsColliderHandle::INVALID;
    }

    FontHandle fontHandle = LoadFont(fontId);
    if (!fontHandle.IsValid()) {
        LOG_ERROR(Asset, "LoadText3DCollider: font {} could not be loaded", fontId.id);
        text3DParamsPool.Remove(paramsHandle);
        return PhysicsColliderHandle::INVALID;
    }

    Text3DParams& params = *pooledParams;
    params.fontId = fontId.id;
    params.text = text;
    params.depth = depth;
    params.flatness = flatness;
    params.tracking = tracking;
    params.scale = scale;
    params.wrapWidth = wrapWidth;
    params.bendRadius = bendRadius;
    params.bSmoothNormals = bSmoothNormals;
    params.align = align;
    params.anchor = anchor;
    params.font = GetFont(fontHandle);

    static std::atomic<int32_t> text3DColliderCounter{0};
    const auto [handle, collider, bCreated] = colliders.Acquire(colliderId, [&](PhysicsColliderAsset& c, PhysicsColliderHandle ch) {
        c.selfHandle = ch;
        c.name = Core::InlineString<128>::Format("Text3D Collider %d", text3DColliderCounter.fetch_add(1, std::memory_order_relaxed));
        c.colliderId = colliderId;
        c.kind = kind;
        c.text3DParams = pooledParams;
        c.bPreciseText3D = bPrecise;
        // Generation-scoped font ref: the worker reads glyph plane bounds; released when the collider finalizes (ResolveLoads).
        c.text3DFontHandle = fontHandle;
        c.loadState = PhysicsColliderAsset::LoadState::NotLoaded;
    });
    if (!bCreated) {
        text3DParamsPool.Remove(paramsHandle);
        UnloadFont(fontHandle);
    }
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate collider slot for 3D text");
        return PhysicsColliderHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    assetLoadManager->RequestPhysicsColliderLoad(collider);
    return handle;
}

PhysicsColliderAsset* AssetManager::GetCollider(PhysicsColliderHandle handle)
{
    return colliders.Get(handle);
}

const PhysicsColliderAsset* AssetManager::GetCollider(PhysicsColliderHandle handle) const
{
    return colliders.Get(handle);
}

void AssetManager::UnloadCollider(PhysicsColliderHandle handle)
{
    if (!colliders.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid collider handle");
    }
}

void AssetManager::IdleLogCounter::Tick(int32_t thisTick, const char* what)
{
    const auto now = std::chrono::steady_clock::now();
    if (thisTick > 0) {
        pending += thisTick;
        lastActivity = now;
        return;
    }
    if (pending > 0 && now - lastActivity >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Asset, "{} {}", pending, what);
        pending = 0;
    }
}

ResolveLoadResult AssetManager::ResolveLoads(Core::FrameBuffer& stagingFrameBuffer)
{
    ResolveLoadResult loadCounts{};
    AssetLoad::StaticModelLoadComplete complete{};
    int32_t modelsThisTick{0};
    while (assetLoadManager->TryDequeueModelComplete(complete)) {
        if (complete.bSuccess) {
            stagingFrameBuffer.imageAcquireOperations.Insert(stagingFrameBuffer.imageAcquireOperations.end(),
                                                             complete.model->imageAcquireOps.begin(),
                                                             complete.model->imageAcquireOps.end());

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
            complete.model->imageAcquireOps.Clear();
            complete.model->modelLoadState = StaticModel::ModelLoadState::FailedToLoad;
            LOG_ERROR(Asset, "Model load failed: {}", complete.model->name.c_str());
        }
    }

    AssetLoad::StaticModelLoadComplete proceduralComplete{};
    while (assetLoadManager->TryDequeueProceduralModelComplete(proceduralComplete)) {
        if (proceduralComplete.bSuccess) {
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
            proceduralComplete.model->imageAcquireOps.Clear();
            proceduralComplete.model->modelLoadState = StaticModel::ModelLoadState::FailedToLoad;
            LOG_ERROR(Asset, "Procedural model generation failed: {}", proceduralComplete.model->name.c_str());
        }
        // The generation worker is done reading the font; release the generation-scoped ref (Text3D only; invalid for other procedurals).
        if (proceduralComplete.model->text3DFontHandle.IsValid()) {
            UnloadFont(proceduralComplete.model->text3DFontHandle);
            proceduralComplete.model->text3DFontHandle = {};
        }
    }

    AssetLoad::PhysicsColliderLoadComplete colliderComplete{};
    while (assetLoadManager->TryDequeuePhysicsColliderComplete(colliderComplete)) {
        if (colliderComplete.bSuccess) {
            colliderComplete.collider->loadState = PhysicsColliderAsset::LoadState::Loaded;
            colliderComplete.collider->acquireFrame = ctx->currentRenderFrame;
            loadCounts.colliderLoadedCount++;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Physics collider generation succeeded: {}", colliderComplete.collider->name.c_str());
            }
        }
        else {
            colliderComplete.collider->loadState = PhysicsColliderAsset::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Physics collider generation failed: {}", colliderComplete.collider->name.c_str());
        }

        if (colliderComplete.collider->text3DFontHandle.IsValid()) {
            UnloadFont(colliderComplete.collider->text3DFontHandle);
            colliderComplete.collider->text3DFontHandle = {};
        }
    }

    modelLoadLog.Tick(modelsThisTick, "model(s) loaded");

    AssetLoad::TextureLoadComplete textureComplete{};
    int32_t texturesThisTick{0};
    while (assetLoadManager->TryDequeueTextureComplete(textureComplete)) {
        if (textureComplete.bSuccess) {
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

    textureLoadLog.Tick(texturesThisTick, "texture(s) loaded");

    AssetLoad::CubemapLoadComplete cubemapComplete{};
    int32_t cubemapsThisTick{0};
    while (assetLoadManager->TryDequeueCubemapComplete(cubemapComplete)) {
        if (cubemapComplete.bSuccess) {
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

    cubemapLoadLog.Tick(cubemapsThisTick, "cubemap(s) loaded");

    AssetLoad::TextureArrayLoadComplete arrayComplete{};
    while (assetLoadManager->TryDequeueTextureArrayComplete(arrayComplete)) {
        if (arrayComplete.bSuccess) {
            arrayComplete.textureArray->loadState = Render::TextureArray::LoadState::Loaded;
            loadCounts.textureArrayLoadedCount++;
        }
        else {
            arrayComplete.textureArray->loadState = Render::TextureArray::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Texture array load failed: {}", arrayComplete.textureArray->name.c_str());
        }
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

    samplerLoadLog.Tick(samplersThisTick, "sampler(s) loaded");

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

    proceduralTextureLoadLog.Tick(proceduralTexturesThisTick, "procedural texture(s) generated");

    int32_t fontsThisTick{0};
    AssetLoad::FontCurveLoadComplete fontComplete{};
    while (assetLoadManager->TryDequeueFontCurveComplete(fontComplete)) {
        Font* font = fontComplete.font;
        if (fontComplete.bSuccess) {
            if (font->atlasAcquireBarrier.image != 0) {
                stagingFrameBuffer.imageAcquireOperations.PushBack(font->atlasAcquireBarrier);
                font->atlasAcquireBarrier = {};
            }
            font->loadState = Font::LoadState::Loaded;
            if (bVerboseLogging.load(std::memory_order_relaxed)) {
                LOG_TRACE(Asset, "Font loaded: {} (curve byte offset: {})", font->name.c_str(), font->curveByteOffset);
            }
            loadCounts.fontLoadedCount++;
            fontsThisTick++;
        }
        else {
            font->loadState = Font::LoadState::FailedToLoad;
            LOG_ERROR(Asset, "Font curve upload failed: {}", font->name.c_str());
        }
    }

    fontLoadLog.Tick(fontsThisTick, "font(s) loaded");

    return loadCounts;
}

void AssetManager::KickOffRetires()
{
    const uint64_t currentFrame = ctx->currentRenderFrame;
    constexpr uint64_t retireDelay = Core::FRAME_BUFFER_COUNT * 4;
    textures.KickOffRetires(currentFrame, retireDelay, [](const Texture& texture) {
        return texture.loadState == Texture::LoadState::Loaded || texture.loadState == Texture::LoadState::FailedToLoad;
    });
    models.KickOffRetires(currentFrame, retireDelay, [](const StaticModel& model) {
        return model.modelLoadState == StaticModel::ModelLoadState::Loaded || model.modelLoadState == StaticModel::ModelLoadState::FailedToLoad;
    });
    fonts.KickOffRetires(currentFrame, retireDelay, [](const Font& font) {
        return font.loadState == Font::LoadState::Loaded || font.loadState == Font::LoadState::FailedToLoad;
    });
    colliders.KickOffRetires(currentFrame, retireDelay, [](const PhysicsColliderAsset& collider) {
        return collider.loadState == PhysicsColliderAsset::LoadState::Loaded || collider.loadState == PhysicsColliderAsset::LoadState::FailedToLoad;
    });
    cubemaps.KickOffRetires(currentFrame, retireDelay, [](const Render::Cubemap& cubemap) {
        return cubemap.loadState != Render::Cubemap::LoadState::Loading;
    });
    textureArrays.KickOffRetires(currentFrame, retireDelay, [](const Render::TextureArray& textureArray) {
        return textureArray.loadState != Render::TextureArray::LoadState::Loading;
    });
    samplers.KickOffRetires(currentFrame, retireDelay, [](const Sampler&) { return true; });
}

bool AssetManager::HasPendingLoads() const
{
    bool bPending = false;
    textures.ForEach([&](TextureHandle, const Texture& texture) { bPending |= texture.loadState == Texture::LoadState::Loading; });
    models.ForEach([&](StaticModelHandle, const StaticModel& model) { bPending |= model.modelLoadState == StaticModel::ModelLoadState::NotLoaded; });
    fonts.ForEach([&](FontHandle, const Font& font) { bPending |= font.loadState == Font::LoadState::Loading; });
    colliders.ForEach([&](PhysicsColliderHandle, const PhysicsColliderAsset& collider) { bPending |= collider.loadState == PhysicsColliderAsset::LoadState::NotLoaded; });
    cubemaps.ForEach([&](CubemapHandle, const Render::Cubemap& cubemap) { bPending |= cubemap.loadState == Render::Cubemap::LoadState::Loading; });
    textureArrays.ForEach([&](TextureArrayHandle, const Render::TextureArray& textureArray) { bPending |= textureArray.loadState == Render::TextureArray::LoadState::Loading; });
    return bPending;
}

void AssetManager::LogPendingLoads() const
{
    textures.ForEach([this](TextureHandle handle, const Texture& texture) {
        if (texture.loadState == Texture::LoadState::Loading) {
            LOG_WARN(Asset, "Pending load: texture '{}' (refCount {})", texture.name.c_str(), textures.RefCount(handle));
        }
    });
    models.ForEach([this](StaticModelHandle handle, const StaticModel& model) {
        if (model.modelLoadState == StaticModel::ModelLoadState::NotLoaded) {
            LOG_WARN(Asset, "Pending load: model '{}' (refCount {})", model.name.c_str(), models.RefCount(handle));
        }
    });
    fonts.ForEach([this](FontHandle handle, const Font& font) {
        if (font.loadState == Font::LoadState::Loading) {
            LOG_WARN(Asset, "Pending load: font '{}' (refCount {})", font.name.c_str(), fonts.RefCount(handle));
        }
    });
    colliders.ForEach([this](PhysicsColliderHandle handle, const PhysicsColliderAsset& collider) {
        if (collider.loadState == PhysicsColliderAsset::LoadState::NotLoaded) {
            LOG_WARN(Asset, "Pending load: collider '{}' (refCount {})", collider.name.c_str(), colliders.RefCount(handle));
        }
    });
    cubemaps.ForEach([this](CubemapHandle handle, const Render::Cubemap& cubemap) {
        if (cubemap.loadState == Render::Cubemap::LoadState::Loading) {
            LOG_WARN(Asset, "Pending load: cubemap '{}' (refCount {})", cubemap.name.c_str(), cubemaps.RefCount(handle));
        }
    });
    textureArrays.ForEach([this](TextureArrayHandle handle, const Render::TextureArray& textureArray) {
        if (textureArray.loadState == Render::TextureArray::LoadState::Loading) {
            LOG_WARN(Asset, "Pending load: texture array '{}' (refCount {})", textureArray.name.c_str(), textureArrays.RefCount(handle));
        }
    });
}

ResolveUnloadResult AssetManager::ResolveUnloads()
{
    const uint64_t currentFrame = ctx->currentRenderFrame;

    for (size_t i = deferredTextureBindingReleases.Size(); i > 0;) {
        --i;
        if (currentFrame < deferredTextureBindingReleases[i].releaseFrame) { continue; }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(deferredTextureBindingReleases[i].handle);
        deferredTextureBindingReleases.SwapRemove(i);
    }
    for (size_t i = deferredCubemapBindingReleases.Size(); i > 0;) {
        --i;
        if (currentFrame < deferredCubemapBindingReleases[i].releaseFrame) { continue; }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseCubemapBinding(deferredCubemapBindingReleases[i].handle);
        deferredCubemapBindingReleases.SwapRemove(i);
    }
    for (size_t i = deferredTextureArrayReleases.Size(); i > 0;) {
        --i;
        if (currentFrame < deferredTextureArrayReleases[i].releaseFrame) { continue; }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureArrayBinding(deferredTextureArrayReleases[i].handle);
        deferredTextureArrayReleases.SwapRemove(i);
    }

    const int32_t modelsUnloadedThisTick = models.Reclaim(currentFrame, [this](StaticModel& model, ModelID modelId) {
        if (model.modelLoadState == StaticModel::ModelLoadState::Loaded) {
            model.modelData.Reset(resourceManager);
        }
        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Model unloaded: {}", model.name.c_str()); }
        UnfreezeModel(modelId); // drained: lift any hot-reload freeze
        assert(!model.text3DFontHandle.IsValid() && "Text3D font ref must be released at finalize (ResolveLoads) before a model can be reclaimed");
        if (model.moduleParams != nullptr) {
            moduleParamsPool.Remove(moduleParamsPool.HandleOf(model.moduleParams));
        }
        if (model.splineParams != nullptr) {
            splineParamsPool.Remove(splineParamsPool.HandleOf(model.splineParams));
        }
        if (model.text3DParams != nullptr) {
            text3DParamsPool.Remove(text3DParamsPool.HandleOf(model.text3DParams));
        }
    });

    modelUnloadLog.Tick(modelsUnloadedThisTick, "model(s) unloaded");

    colliders.Reclaim(currentFrame, [this](PhysicsColliderAsset& collider, PhysicsColliderID) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Collider unloaded: {}", collider.name.c_str()); }
        if (collider.splineParams != nullptr) {
            splineParamsPool.Remove(splineParamsPool.HandleOf(collider.splineParams));
        }
        if (collider.text3DParams != nullptr) {
            text3DParamsPool.Remove(text3DParamsPool.HandleOf(collider.text3DParams));
        }
    });

    const int32_t texturesUnloadedThisTick = textures.Reclaim(currentFrame, [this](Texture& texture, TextureID) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Texture unloaded: {} (bindless index: {})", texture.name.c_str(), static_cast<uint32_t>(texture.bindlessHandle.index));
        }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(texture.bindlessHandle);
    });

    textureUnloadLog.Tick(texturesUnloadedThisTick, "texture(s) unloaded");

    cubemaps.Reclaim(currentFrame, [this](Render::Cubemap& cubemap, EnvironmentMapID) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Cubemap unloaded: {} (bindless index: {})", cubemap.name.c_str(), static_cast<uint32_t>(cubemap.bindlessHandle.index));
        }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseCubemapBinding(cubemap.bindlessHandle);
    });

    textureArrays.Reclaim(currentFrame, [this](Render::TextureArray& textureArray, TextureArrayID) {
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureArrayBinding(textureArray.bindlessHandle);
    });

    const int32_t samplersUnloadedThisTick = samplers.Reclaim(currentFrame, [this](Sampler& sampler, SamplerID) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Sampler unloaded (bindless index: {})", static_cast<uint32_t>(sampler.bindlessHandle.index)); }
        resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseSamplerBinding(sampler.bindlessHandle);
    });

    samplerUnloadLog.Tick(samplersUnloadedThisTick, "sampler(s) unloaded");

    const int32_t fontsUnloadedThisTick = fonts.Reclaim(currentFrame, [this](Font& font, FontID fontId) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) { LOG_TRACE(Asset, "Font unloaded: {}", font.name.c_str()); }
        if (font.loadState != Font::LoadState::Loading && font.curveAllocation.offset != OffsetAllocator::Allocation::NO_SPACE) {
            std::lock_guard lock(resourceManager->fontCurveAllocatorMutex);
            resourceManager->fontCurveAllocator.free(font.curveAllocation);
        }
        if (font.header.sdfUncompressedSize > 0) {
            resourceManager->bindlessSamplerTextureDescriptorBuffer.ReleaseTextureBinding(font.atlasBindlessHandle);
        }
        UnfreezeFont(fontId); // drained: lift any hot-reload freeze
    });

    fontUnloadLog.Tick(fontsUnloadedThisTick, "font(s) unloaded");

    return {.modelUnloadedCount = modelsUnloadedThisTick, .fontUnloadedCount = fontsUnloadedThisTick};
}

void AssetManager::Scan(Core::Span<const StringID> loadedScenes)
{
    changedModelIds.Clear();
    changedTextureIds.Clear();
    changedFontIds.Clear();
    changedEnvironmentMapIds.Clear();
    changedTextureArrayIds.Clear();

    const bool bWalkAssets = ctx->rescan.bResources;
    if (bWalkAssets || ctx->rescan.bScenes) {
        const Core::Path& assetPath = Platform::GetAssetPath();
        const Core::Path& scenePath = Platform::GetScenePath();
        const uint32_t generation = ++scanGeneration;
        if ((bWalkAssets && Platform::FileExists(assetPath)) || Platform::FileExists(scenePath)) {
            Core::Vector<Core::Path> paths;
            paths = Core::Vector<Core::Path>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
            if (bWalkAssets && Platform::FileExists(assetPath)) { Platform::RecursiveDirectoryIterator(assetPath, paths); }
            if (Platform::FileExists(scenePath)) { Platform::RecursiveDirectoryIterator(scenePath, paths); }

            const auto skipNoId = [](const char* kind, const Core::Path& path) {
                LOG_WARN(Asset, "{} '{}' has no id. Regenerate it to fix", kind, path.c_str());
            };

            for (const auto& path : paths) {
                const auto ext = path.Extension();

                if (ext == ".wtexture") {
                    auto header = ReadWTextureHeader(path);
                    if (!header) { continue; }
                    if (header->textureId == 0) {
                        skipNoId("Texture", path);
                        continue;
                    }
                    const TextureID id{header->textureId};
                    const Core::InlineString<128> name{header->name};
                    const auto [cached, bExisted, prevVersion] = textureRegistry.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
                    cached = {};
                    cached.source = Core::Path(path);
                    cached.name = name;
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cached.compressionType = header->compressionType;
                    cached.contentVersion = header->contentVersion;
                    cached.bUngenerated = header->bUngenerated;
                    cached.category = header->category;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Texture '{}' (id {:x}) content changed on disk: v{} -> v{}", name.c_str(), id.id, prevVersion, header->contentVersion);
                        changedTextureIds.PushBack(id);
                    }
                }
                else if (ext == ".wsmesh") {
                    auto optModelHeader = ReadWStaticModelHeader(path);
                    if (!optModelHeader) { continue; }
                    if (optModelHeader->modelId == 0) {
                        skipNoId("Model", path);
                        continue;
                    }
                    auto optNodes = ReadWStaticModelNodes(path, *optModelHeader, memoryManager->Assets());
                    if (!optNodes) { continue; }

                    const ModelID id{optModelHeader->modelId};
                    const Core::InlineString<128> name{optModelHeader->name};
                    const auto [cached, bExisted, prevVersion] = modelCache.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
                    cached.source = Core::Path(path);
                    cached.name = name;
                    cached.nodeCount = optModelHeader->nodeCount;
                    cached.meshNodesCount = optModelHeader->meshNodeCount;
                    cached.nodes = std::move(optNodes->nodes);
                    cached.bounds = optNodes->bounds;
                    cached.contentVersion = optModelHeader->contentVersion;
                    cached.uncompressedBodySize = optModelHeader->uncompressedBodySize;
                    if (bExisted && prevVersion != optModelHeader->contentVersion) {
                        LOG_TRACE(Asset, "Model '{}' (id {:x}) content changed on disk: v{} -> v{}", name.c_str(), id.id, prevVersion, optModelHeader->contentVersion);
                        changedModelIds.PushBack(id);
                    }
                }
                else if (ext == ".wenvmap") {
                    auto header = ReadWEnvMapHeader(path);
                    if (!header) { continue; }
                    if (header->environmentMapId == 0) {
                        skipNoId("Environment map", path);
                        continue;
                    }
                    const EnvironmentMapID id{header->environmentMapId};
                    const Core::InlineString<128> name{header->name};
                    const auto [cached, bExisted, prevVersion] = cubemapCache.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
                    cached.source = Core::Path(path);
                    cached.name = name;
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cached.compressionType = header->compressionType;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Cubemap '{}' (id {:x}) content changed on disk: v{} -> v{}", name.c_str(), id.id, prevVersion, header->contentVersion);
                        changedEnvironmentMapIds.PushBack(id);
                    }
                }
                else if (ext == ".wprobe") {
                    auto header = ReadWProbeHeader(path);
                    if (!header) { continue; }
                    if (header->probeId == 0 || header->environmentMapId == 0) {
                        skipNoId("Probe", path);
                        continue;
                    }
                    const ProbeID probeId{header->probeId};
                    const EnvironmentMapID envMapId{header->environmentMapId};

                    const auto [probeInfo, bExisted, prevVersion] = probeRegistry.Stamp(probeId, {}, generation, path);
                    probeInfo.source = Core::Path(path);
                    probeInfo.environmentMapId = envMapId;
                    probeInfo.resolution = header->resolution;
                    probeInfo.snapshot = header->snapshot;
                    probeInfo.contentVersion = header->contentVersion;
                    probeInfo.radianceScale = header->radianceScale;

                    // A probe is a cubemap at runtime: its paired cubemap entry (keyed by EnvironmentMapID) lets LoadCubemap and cubemap hot reload cover probes. Stamped together, so they prune together.
                    CachedCubemapMetadata& cached = cubemapCache.Stamp(envMapId, {}, generation, path).entry;
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(header->name);
                    cached.width = header->width;
                    cached.height = header->height;
                    cached.mipCount = header->mipCount;
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cached.compressionType = header->compressionType;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Probe '{}' (id {:x}) content changed on disk: v{} -> v{}", cached.name.c_str(), envMapId.id, prevVersion, header->contentVersion);
                        changedEnvironmentMapIds.PushBack(envMapId);
                    }
                }
                else if (ext == ".wshadowmap") {
                    auto header = ReadWShadowMapHeader(path);
                    if (!header) { continue; }
                    if (header->shadowId == 0) {
                        skipNoId("Shadow map", path);
                        continue;
                    }
                    const TextureArrayID arrayId{header->shadowId};

                    const auto [info, bExisted, prevVersion] = shadowMapRegistry.Stamp(arrayId, {}, generation, path);
                    info.source = Core::Path(path);
                    info.resolution = header->resolution;
                    info.key = header->key;
                    info.contentVersion = header->contentVersion;

                    // Paired texture array entry, stamped together like probes and their cubemaps.
                    CachedTextureArrayMetadata& cached = textureArrayCache.Stamp(arrayId, {}, generation, path).entry;
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(path.Stem());
                    cached.dataOffset = header->dataOffset;
                    cached.dataSize = header->dataSize;
                    cached.uncompressedSize = header->uncompressedSize;
                    cached.compressionType = header->compressionType;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        changedTextureArrayIds.PushBack(arrayId);
                    }
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
                    const auto [cached, bExisted, prevVersion] = sceneCache.Stamp(id, {}, generation, path);
                    cached.source = Core::Path(path);
                    cached.sceneName = Core::InlineString<128>(path.Stem());
                    cached.entityCount = header->entityCount;
                    cached.contentVersion = header->contentVersion;
                    cached.bUnsaved = false;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Scene '{}' (id {:x}) content changed on disk: v{} -> v{}", cached.sceneName.c_str(), id.id, prevVersion, header->contentVersion);
                    }
                }
                else if (ext == ".wsfont") {
                    auto header = ReadWFontHeader(path);
                    if (!header) { continue; }
                    if (header->fontId == 0) {
                        skipNoId("Font", path);
                        continue;
                    }
                    const FontID id{header->fontId};
                    const Core::InlineString<128> name{header->name};
                    const auto [cached, bExisted, prevVersion] = fontCache.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
                    cached.source = path;
                    cached.name = name;
                    cached.header = *header;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Font '{}' (id {:x}) content changed on disk: v{} -> v{}", name.c_str(), id.id, prevVersion, header->contentVersion);
                        changedFontIds.PushBack(id);
                    }
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
                    const auto [cached, bExisted, prevVersion] = prefabCache.Stamp(id, {}, generation, path);
                    cached.source = Core::Path(path);
                    cached.prefabName = Core::InlineString<128>(header->name);
                    cached.componentCount = header->componentCount;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Prefab '{}' (id {:x}) content changed on disk: v{} -> v{}", cached.prefabName.c_str(), id.id, prevVersion, header->contentVersion);
                    }
                }
                else if (ext == ".wplay") {
                    auto header = ReadWPlayHeader(path);
                    if (!header) { continue; }
                    const StringID id{path.c_str(), strlen(path.c_str())};
                    const auto [cached, bExisted, prevVersion] = playCache.Stamp(id, {}, generation, path);
                    cached.source = Core::Path(path);
                    cached.name = Core::InlineString<128>(header->name);
                    cached.sceneId = StringID{header->sceneId};
                    cached.eventCount = header->eventCount;
                    cached.contentVersion = header->contentVersion;
                    if (bExisted && prevVersion != header->contentVersion) {
                        LOG_TRACE(Asset, "Run '{}' content changed on disk: v{} -> v{}", cached.name.c_str(), prevVersion, header->contentVersion);
                    }
                }
            }
        }

        sceneFolders.Clear();
        if (Platform::FileExists(scenePath)) {
            Core::Vector<Core::Path> folders(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
            Platform::RecursiveSubdirectories(scenePath, folders);
            for (const Core::Path& folder : folders) {
                if (sceneFolders.IsFull()) {
                    LOG_WARN(Asset, "More than {} scene folders; the rest are hidden in the editor", sceneFolders.GetCapacity());
                    break;
                }
                sceneFolders.PushBack(folder);
            }
        }

        // Asset kinds are only seen when the asset tree was walked; resident assets keep their entry until they unload.
        if (bWalkAssets) {
            modelCache.Prune(generation, [this](ModelID id, const CachedModelMetadata&) { return models.Contains(id); });
            textureRegistry.Prune(generation, [this](TextureID id, const CachedTextureMetadata&) { return textures.Contains(id); });
            cubemapCache.Prune(generation, [this](EnvironmentMapID id, const CachedCubemapMetadata&) { return cubemaps.Contains(id); });
            probeRegistry.Prune(generation, [this](ProbeID, const ProbeInfo& probe) { return cubemaps.Contains(probe.environmentMapId); });
            textureArrayCache.Prune(generation, [this](TextureArrayID id, const CachedTextureArrayMetadata&) { return textureArrays.Contains(id); });
            shadowMapRegistry.Prune(generation, [this](TextureArrayID id, const ShadowMapInfo&) { return textureArrays.Contains(id); });
            fontCache.Prune(generation, [this](FontID id, const CachedFontMetadata&) { return fonts.Contains(id); });
            prefabCache.Prune(generation, [](StringID, const CachedPrefabMetadata&) { return false; });
        }
        sceneCache.Prune(generation, [&loadedScenes](StringID id, CachedSceneMetadata& meta) {
            if (std::ranges::find(loadedScenes, id) == loadedScenes.end()) { return false; }
            if (!meta.bUnsaved) {
                LOG_WARN(Asset, "Scene '{}' is loaded but its file is gone from disk; saving writes it back to {}", meta.sceneName.c_str(), meta.source.c_str());
                meta.bUnsaved = true;
            }
            return true;
        });
        playCache.Prune(generation, [](StringID, const CachedPlayMetadata&) { return false; });

        modelCache.RebuildNames();
        textureRegistry.RebuildNames();
        cubemapCache.RebuildNames();
        fontCache.RebuildNames();
    }

    ctx->rescan.bResources = false;
    ctx->rescan.bScenes = false;
}

void AssetManager::RegisterProceduralTextures()
{
    auto addProceduralToRegistry = [&](Core::InlineString<128> name, StringID pipelineID, uint32_t width, uint32_t height, VkFormat format, bool mipmapped) {
        const TextureID texId{pipelineID.id};
        CachedTextureMetadata& desc = textureRegistry.Stamp(texId, StringID{name.c_str(), name.Size()}, CatalogTable<TextureID, CachedTextureMetadata>::PERMANENT, {}).entry;
        desc.name = name;
        desc.width = width;
        desc.height = height;
        desc.mipCount = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1u : 1u;
        desc.category = TextureCategory::Procedural;
        desc.origin = Texture::Origin::StaticProcedural;
        desc.pipelineId = pipelineID;
        desc.format = format;
        desc.bMipmapped = mipmapped;
    };

    addProceduralToRegistry(Core::InlineString<128>("yellow_texture"), "yellow_texture"_sid, 256, 256, VK_FORMAT_R8G8B8A8_UNORM, true);
    addProceduralToRegistry(Core::InlineString<128>("domain_warp"), "domain_warp"_sid, 512, 512, VK_FORMAT_R8G8B8A8_UNORM, true);
    addProceduralToRegistry(Core::InlineString<128>("greybox"), "greybox"_sid, 1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, true);
    textureRegistry.RebuildNames();
}

Texture* AssetManager::LoadTexture(TextureID textureId)
{
    const CachedTextureMetadata* meta = textureRegistry.Find(textureId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "LoadTexture: TextureID {:x} not found in the texture catalog", textureId.id);
        return nullptr;
    }
    if (meta->origin == Texture::Origin::StaticProcedural) {
        return LoadProceduralTexture(meta->pipelineId, meta->width, meta->height, meta->format, meta->bMipmapped, Texture::Origin::StaticProcedural, meta->name);
    }
    return LoadDiskTexture(textureId);
}

Texture* AssetManager::LoadDiskTexture(TextureID textureId)
{
    const CachedTextureMetadata* meta = textureRegistry.Find(textureId);
    if (meta == nullptr || meta->origin != Texture::Origin::Disk) {
        LOG_ERROR(Asset, "LoadDiskTexture: TextureID {:x} not in disk cache", textureId.id);
        return nullptr;
    }

    if (const TextureHandle existing = textures.AddRef(textureId); existing.IsValid()) {
        Texture* texture = textures.Get(existing);
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Texture already loaded: {}, refCount: {}", texture->name.c_str(), textures.RefCount(existing));
        }
        return texture;
    }

    if (meta->bUngenerated) {
        LOG_WARN(Asset, "Texture '{}' is declared but not generated yet", meta->name.c_str());
        return nullptr;
    }

    const auto [handle, texture, bCreated] = textures.Acquire(textureId, [&](Texture& t, TextureHandle th) {
        t.selfHandle = th;
        t.source = meta->source;
        t.textureId = textureId;
        t.name = Core::InlineString(meta->name);
        t.width = meta->width;
        t.height = meta->height;
        t.mipCount = meta->mipCount;
        t.dataOffset = meta->dataOffset;
        t.dataSize = meta->dataSize;
        t.uncompressedSize = meta->uncompressedSize;
        t.compressionType = meta->compressionType;
        t.loadState = Texture::LoadState::Loading;
        t.loadedContentVersion = meta->contentVersion;
        t.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate texture slot for {:x}", textureId.id);
        return nullptr;
    }
    if (!bCreated) { return texture; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting texture load: {}", texture->name.c_str());
    }
    assetLoadManager->RequestTextureLoad(texture);

    return texture;
}

Texture* AssetManager::LoadProceduralTexture(StringID pipelineId, uint32_t width, uint32_t height, VkFormat format, bool mipmapped, Texture::Origin origin, Core::InlineString<128> displayName)
{
    const TextureID textureId{pipelineId.id};

    const uint32_t mipCount = mipmapped ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height))))) + 1u : 1u;
    const Core::InlineString<128> name = displayName.IsEmpty() ? Core::InlineString<128>::Format("procedural_%llu", textureId.id) : displayName;

    const auto [handle, texture, bCreated] = textures.Acquire(textureId, [&](Texture& t, TextureHandle th) {
        t.selfHandle = th;
        t.textureId = textureId;
        t.name = name;
        t.width = width;
        t.height = height;
        t.mipCount = mipCount;
        t.format = format;
        t.loadState = Texture::LoadState::Loading;
        t.origin = origin;
        t.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "LoadProceduralTexture: no texture slots available");
        return nullptr;
    }
    if (!bCreated) { return texture; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting procedural texture load: {:x} ({}x{}, {} mips)", textureId.id, width, height, mipCount);
    }
    assetLoadManager->RequestProceduralTextureLoad(texture, pipelineId);

    return texture;
}

void AssetManager::GetAllTextureInfos(Core::ArenaFixedMap<TextureID, EditorTextureInfo>& out) const
{
    for (const auto& [texId, desc] : textureRegistry) {
        uint32_t mipCount = desc.mipCount;
        if (desc.origin == Texture::Origin::StaticProcedural) {
            if (const Texture* loaded = textures.Get(textures.Find(texId))) {
                mipCount = loaded->mipCount;
            }
        }
        out[texId] = {texId, desc.name, desc.width, desc.height, mipCount, desc.category};
    }
    textures.ForEach([&out](TextureHandle, const Texture& tex) {
        if (tex.origin == Texture::Origin::RuntimeProcedural) {
            out[tex.textureId] = {tex.textureId, tex.name, tex.width, tex.height, tex.mipCount, TextureCategory::Procedural};
        }
    });
}

bool AssetManager::ReloadProceduralTexture(TextureID textureId)
{
    Texture* texture = textures.Get(textures.Find(textureId));
    if (texture == nullptr) {
        return false;
    }

    if (texture->origin == Texture::Origin::Disk) {
        return false;
    }
    if (texture->loadState == Texture::LoadState::Loading) {
        LOG_WARN(Asset, "Procedural texture '{}' reload requested while still generating; skipping", texture->name.c_str());
        return false;
    }

    texture->loadState = Texture::LoadState::Loading;
    deferredTextureBindingReleases.PushBack({texture->bindlessHandle, ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4, std::move(texture->image), std::move(texture->imageView)});
    texture->bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    LOG_INFO(Asset, "Regenerating procedural texture '{}'", texture->name.c_str());
    assetLoadManager->RequestProceduralTextureLoad(texture, StringID{textureId.id});

    return true;
}

bool AssetManager::ReloadTexture(TextureID textureId)
{
    const CachedTextureMetadata* metaPtr = textureRegistry.Find(textureId);
    if (metaPtr == nullptr || metaPtr->origin != Texture::Origin::Disk) {
        if (ReloadProceduralTexture(textureId)) {
            return true;
        }
        LOG_ERROR(Asset, "Texture {:x} not found in registry", textureId.id);
        return false;
    }

    const TextureHandle handle = textures.Find(textureId);
    Texture* texturePtr = textures.Get(handle);
    if (texturePtr == nullptr) {
        LOG_ERROR(Asset, "Texture {:x} requested reload but it is not currently loaded", textureId.id);
        return false;
    }

    const CachedTextureMetadata& meta = *metaPtr;

    Texture& texture = *texturePtr;
    if (texture.loadedContentVersion == meta.contentVersion) {
        return false;
    }
    if (texture.loadState == Texture::LoadState::Loading) {
        LOG_WARN(Asset, "Texture '{}' reload requested while still loading; skipping", texture.name.c_str());
        return false;
    }
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
    texture.loadedContentVersion = meta.contentVersion;
    deferredTextureBindingReleases.PushBack({texture.bindlessHandle, ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4});
    texture.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting texture reload: {}", texture.name.c_str());
    }
    assetLoadManager->RequestTextureLoad(&texture);

    return true;
}

void AssetManager::UnloadTexture(TextureID id)
{
    const TextureHandle handle = textures.Find(id);
    if (!textures.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload texture that is not loaded");
        return;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Texture refCount decremented: {}, refCount: {}", textures.Get(handle)->name.c_str(), textures.RefCount(handle));
    }
}

Sampler* AssetManager::LoadSampler(SamplerDesc& samplerDesc)
{
    const SamplerID id{Hash(&samplerDesc, sizeof(samplerDesc))};

    const auto [handle, sampler, bCreated] = samplers.Acquire(id, [&](Sampler& s, SamplerHandle sh) {
        s.desc = samplerDesc;
        s.id = id;
        s.selfHandle = sh;
        s.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateSampler();
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate sampler slot");
        return nullptr;
    }
    if (!bCreated) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Sampler already loaded (bindless index: {}), refCount: {}", static_cast<uint32_t>(sampler->bindlessHandle.index), samplers.RefCount(handle));
        }
        return sampler;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting sampler load (bindless index: {})", static_cast<uint32_t>(sampler->bindlessHandle.index));
    }
    assetLoadManager->RequestSamplerLoad(sampler);

    return sampler;
}

void AssetManager::UnloadSampler(SamplerDesc& desc)
{
    const SamplerID id{Hash(&desc, sizeof(desc))};

    const SamplerHandle handle = samplers.Find(id);
    if (!samplers.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload sampler that is not loaded");
        return;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Sampler refCount decremented: {}, refCount: {}", id.id, samplers.RefCount(handle));
    }
}

CubemapHandle AssetManager::LoadCubemap(EnvironmentMapID cubemapId)
{
    if (!cubemapId.IsValid()) {
        LOG_ERROR(Asset, "LoadCubemap called with invalid EnvironmentMapID");
        return CubemapHandle::INVALID;
    }

    const CachedCubemapMetadata* meta = cubemapCache.Find(cubemapId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Cubemap {:x} not found in registry", cubemapId.id);
        return CubemapHandle::INVALID;
    }

    const auto [handle, cubemap, bCreated] = cubemaps.Acquire(cubemapId, [&](Render::Cubemap& c, CubemapHandle ch) {
        c.source = meta->source;
        c.name = meta->name;
        c.cubemapId = cubemapId;
        c.selfHandle = ch;
        c.dataOffset = meta->dataOffset;
        c.dataSize = meta->dataSize;
        c.uncompressedSize = meta->uncompressedSize;
        c.compressionType = meta->compressionType;
        c.loadState = Render::Cubemap::LoadState::Loading;
        c.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateCubemap();
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate cubemap slot for: {:x}", cubemapId.id);
        return CubemapHandle::INVALID;
    }
    if (!bCreated) {
        LOG_TRACE(Asset, "Cubemap already loaded: {}, refCount: {}", cubemap->name.c_str(), cubemaps.RefCount(handle));
        return handle;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting cubemap load: {}", cubemap->name.c_str());
    }
    assetLoadManager->RequestCubemapLoad(cubemap);

    return handle;
}

bool AssetManager::ReloadCubemap(EnvironmentMapID cubemapId)
{
    const CachedCubemapMetadata* meta = cubemapCache.Find(cubemapId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Cubemap {:x} not found in registry", cubemapId.id);
        return false;
    }

    const CubemapHandle handle = cubemaps.Find(cubemapId);
    Render::Cubemap* cubemapPtr = cubemaps.Get(handle);
    if (cubemapPtr == nullptr) {
        LOG_ERROR(Asset, "Cubemap {:x} requested reload but it is not currently loaded", cubemapId.id);
        return false;
    }

    Render::Cubemap& cubemap = *cubemapPtr;
    cubemap.selfHandle = handle;
    cubemap.source = meta->source;
    cubemap.name = Core::InlineString(meta->name);
    cubemap.cubemapId = cubemapId;
    cubemap.dataOffset = meta->dataOffset;
    cubemap.dataSize = meta->dataSize;
    cubemap.uncompressedSize = meta->uncompressedSize;
    cubemap.compressionType = meta->compressionType;
    cubemap.loadState = Render::Cubemap::LoadState::Loading;
    deferredCubemapBindingReleases.PushBack({cubemap.bindlessHandle, ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4});
    cubemap.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateCubemap();

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting cubemap reload: {}", cubemap.name.c_str());
    }
    assetLoadManager->RequestCubemapLoad(&cubemap);

    return true;
}

Render::Cubemap* AssetManager::GetCubemap(CubemapHandle handle)
{
    return cubemaps.Get(handle);
}

const Render::Cubemap* AssetManager::GetCubemap(CubemapHandle handle) const
{
    return cubemaps.Get(handle);
}

void AssetManager::UnloadCubemap(CubemapHandle handle)
{
    if (!cubemaps.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid cubemap handle");
        return;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Cubemap refCount decremented: {}, refCount: {}", cubemaps.Get(handle)->name.c_str(), cubemaps.RefCount(handle));
    }
}

TextureArrayHandle AssetManager::LoadTextureArray(TextureArrayID textureArrayId)
{
    const CachedTextureArrayMetadata* meta = textureArrayCache.Find(textureArrayId);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Texture array {:x} not found in registry", textureArrayId.id);
        return TextureArrayHandle::INVALID;
    }

    const auto [handle, textureArray, bCreated] = textureArrays.Acquire(textureArrayId, [&](Render::TextureArray& a, TextureArrayHandle ah) {
        a.source = meta->source;
        a.name = meta->name;
        a.textureArrayId = textureArrayId;
        a.selfHandle = ah;
        a.dataOffset = meta->dataOffset;
        a.dataSize = meta->dataSize;
        a.uncompressedSize = meta->uncompressedSize;
        a.compressionType = meta->compressionType;
        a.loadState = Render::TextureArray::LoadState::Loading;
        a.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTextureArray();
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate texture array slot for: {:x}", textureArrayId.id);
        return TextureArrayHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    assetLoadManager->RequestTextureArrayLoad(textureArray);
    return handle;
}

bool AssetManager::ReloadTextureArray(TextureArrayID textureArrayId)
{
    const CachedTextureArrayMetadata* meta = textureArrayCache.Find(textureArrayId);
    Render::TextureArray* textureArrayPtr = textureArrays.Get(textureArrays.Find(textureArrayId));
    if (meta == nullptr || textureArrayPtr == nullptr) {
        return false;
    }

    Render::TextureArray& textureArray = *textureArrayPtr;
    if (textureArray.loadState == Render::TextureArray::LoadState::Loading) {
        LOG_WARN(Asset, "Texture array '{}' reload requested mid-load; skipped", textureArray.name.c_str());
        return false;
    }
    textureArray.source = meta->source;
    textureArray.name = meta->name;
    textureArray.dataOffset = meta->dataOffset;
    textureArray.dataSize = meta->dataSize;
    textureArray.uncompressedSize = meta->uncompressedSize;
    textureArray.compressionType = meta->compressionType;
    textureArray.loadState = Render::TextureArray::LoadState::Loading;
    deferredTextureArrayReleases.PushBack({textureArray.bindlessHandle, ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 4, std::move(textureArray.image), std::move(textureArray.imageView)});
    textureArray.bindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTextureArray();

    assetLoadManager->RequestTextureArrayLoad(&textureArray);
    return true;
}

Render::TextureArray* AssetManager::GetTextureArray(TextureArrayHandle handle)
{
    return textureArrays.Get(handle);
}

const Render::TextureArray* AssetManager::GetTextureArray(TextureArrayHandle handle) const
{
    return textureArrays.Get(handle);
}

void AssetManager::UnloadTextureArray(TextureArrayHandle handle)
{
    if (!textureArrays.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid texture array handle");
    }
}

TextureArrayHandle AssetManager::LoadShadowMap(uint64_t shadowId)
{
    if (!shadowMapRegistry.Contains(TextureArrayID{shadowId})) {
        return TextureArrayHandle::INVALID;
    }
    return LoadTextureArray(TextureArrayID{shadowId});
}

CubemapHandle AssetManager::LoadProbe(ProbeID probeId)
{
    if (!probeId.IsValid()) {
        LOG_ERROR(Asset, "LoadProbe called with invalid ProbeID");
        return CubemapHandle::INVALID;
    }

    const ProbeInfo* info = probeRegistry.Find(probeId);
    if (info == nullptr) {
        LOG_ERROR(Asset, "Probe {:x} not found in registry", probeId.id);
        return CubemapHandle::INVALID;
    }

    return LoadCubemap(info->environmentMapId);
}

void AssetManager::UnloadProbe(CubemapHandle handle)
{
    UnloadCubemap(handle);
}

FontHandle AssetManager::LoadFont(FontID id)
{
    const CachedFontMetadata* meta = fontCache.Find(id);
    if (meta == nullptr) {
        LOG_ERROR(Asset, "Font {:x} not found in registry", id.id);
        return FontHandle::INVALID;
    }

    if (const FontHandle existing = fonts.AddRef(id); existing.IsValid()) {
        if (bVerboseLogging.load(std::memory_order_relaxed)) {
            LOG_TRACE(Asset, "Font already loaded: {}, refCount: {}", fonts.Get(existing)->name.c_str(), fonts.RefCount(existing));
        }
        return existing;
    }

    Font loaded{};
    {
        Platform::ScopedFileMapping map(meta->source);
        if (!map.data || meta->header.glyphDataOffset + meta->header.glyphCount * sizeof(WGlyphInfo) > map.size) {
            LOG_ERROR(Asset, "Failed to open font file: {}", meta->source.c_str());
            return FontHandle::INVALID;
        }
        loaded.glyphs = Core::HeapArray<WGlyphInfo>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta->header.glyphCount);
        memcpy(loaded.glyphs.Data(), map.data + meta->header.glyphDataOffset, meta->header.glyphCount * sizeof(WGlyphInfo));

        // 3D Text
        if (meta->header.contourGlyphCount != 0) {
            if (meta->header.edgeDataOffset + meta->header.edgeCount * sizeof(WFontEdge) > map.size) {
                LOG_ERROR(Asset, "Failed to read contour data for font: {}", meta->name.c_str());
                return FontHandle::INVALID;
            }
            loaded.glyphContourRanges = Core::HeapArray<WGlyphContourRange>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta->header.contourGlyphCount);
            loaded.contourRanges = Core::HeapArray<WContourRange>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta->header.contourCount);
            loaded.contourEdges = Core::HeapArray<WFontEdge>(&memoryManager->Assets(), Core::AllocTag::AssetManager, meta->header.edgeCount);
            memcpy(loaded.glyphContourRanges.Data(), map.data + meta->header.glyphContourRangeOffset, meta->header.contourGlyphCount * sizeof(WGlyphContourRange));
            memcpy(loaded.contourRanges.Data(), map.data + meta->header.contourRangeOffset, meta->header.contourCount * sizeof(WContourRange));
            memcpy(loaded.contourEdges.Data(), map.data + meta->header.edgeDataOffset, meta->header.edgeCount * sizeof(WFontEdge));
        }
    }

    const auto [handle, font, bCreated] = fonts.Acquire(id, [&](Font& f, FontHandle fh) {
        f = std::move(loaded);
        f.selfHandle = fh;
        f.fontId = id;
        f.name = meta->name;
        f.source = meta->source;
        f.header = meta->header;
        f.loadState = Font::LoadState::Loading;
        f.curveAllocation = {};
        f.curveByteOffset = 0;
        f.atlasAcquireBarrier = {};
        if (f.header.sdfUncompressedSize > 0) {
            f.atlasBindlessHandle = resourceManager->bindlessSamplerTextureDescriptorBuffer.ReserveAllocateTexture();
        }
    });
    if (!handle.IsValid()) {
        LOG_ERROR(Asset, "Failed to allocate font slot for {:x}", id.id);
        return FontHandle::INVALID;
    }
    if (!bCreated) { return handle; }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Requesting font curve load: {}", font->name.c_str());
    }
    assetLoadManager->RequestFontCurveLoad(font);

    return handle;
}

void AssetManager::UnloadFont(FontHandle handle)
{
    if (!fonts.Release(handle)) {
        LOG_WARN(Asset, "Attempted to unload invalid font handle");
        return;
    }

    if (bVerboseLogging.load(std::memory_order_relaxed)) {
        LOG_TRACE(Asset, "Font refCount decremented: {}, refCount: {}", fonts.Get(handle)->name.c_str(), fonts.RefCount(handle));
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
    return fonts.Get(handle);
}

const Font* AssetManager::GetFont(FontHandle handle) const
{
    return fonts.Get(handle);
}

const WGlyphInfo* AssetManager::GetGlyph(FontHandle handle, uint32_t codepoint) const
{
    const Font* font = fonts.Get(handle);
    if (font == nullptr || font->loadState != Font::LoadState::Loaded) {
        return nullptr;
    }

    for (uint32_t i = 0; i < font->glyphs.Size(); ++i) {
        if (font->glyphs[i].codepoint == codepoint) {
            return &font->glyphs[i];
        }
    }
    return nullptr;
}
} // Engine
