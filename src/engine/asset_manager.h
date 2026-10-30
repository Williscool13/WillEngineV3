//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H

#include "asset_loader.h"
#include "asset_manager_config.h"
#include "asset_pool.h"
#include "engine/include/engine_context.h"
#include "core/containers/concurrent_slot_pool.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/containers/inline_map.h"
#include "core/containers/vector.h"
#include "logging/engine_log.h"

namespace AssetLoad
{
class AsyncAssetLoadManager;
}

namespace Render
{
struct ResourceManager;
}

namespace Engine
{
struct ResolveLoadResult
{
    int32_t modelLoadedCount{0};
    int32_t textureLoadedCount{0};
    int32_t cubeLoadedCount{0};
    int32_t textureArrayLoadedCount{0};
    int32_t samplerLoadedCount{0};
    int32_t fontLoadedCount{0};
    int32_t colliderLoadedCount{0};
};

struct ResolveUnloadResult
{
    int32_t modelUnloadedCount{0};
    int32_t fontUnloadedCount{0};
};

class AssetManager final : public IAssetLoader
{
public:
    static constexpr const char* COMPONENT_NAME = "AssetManager";
    static constexpr bool EXCLUSIVE_ONLY = true;

    AssetManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager);

    ~AssetManager();

    AssetManager(const AssetManager&) = delete;

    AssetManager& operator=(const AssetManager&) = delete;

    AssetManager(AssetManager&&) = delete;

    AssetManager& operator=(AssetManager&&) = delete;

public: // IAssetLoader
    [[nodiscard]] ModelID FindModelByName(std::string_view name) const override { return FindByName(modelCache, name); }
    [[nodiscard]] uint32_t GetActiveModelCount() const override { return models.Count(); }
    [[nodiscard]] uint32_t GetActiveTextureCount() const override { return textures.Count(); }
    [[nodiscard]] uint32_t GetActiveSamplerCount() const override { return samplers.Count(); }
    [[nodiscard]] uint32_t GetActiveCubemapCount() const override { return cubemaps.Count(); }
    [[nodiscard]] uint32_t GetActiveTextureArrayCount() const override { return textureArrays.Count(); }
    StaticModelHandle LoadModel(ModelID modelId) override;
    StaticModelHandle LoadProceduralModel(const ProceduralParams& params, const ProceduralRepeat& repeat = {}) override;
    StaticModelHandle LoadSplineModel(const SplineParams& params) override;
    StaticModelHandle LoadModuleModel(const ModuleParams& params) override;
    StaticModelHandle LoadText3DModel(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius) override;
    StaticModel* GetModel(StaticModelHandle handle) override;
    const StaticModel* GetModel(StaticModelHandle handle) const override;
    void UnloadModel(StaticModelHandle handle) override;
    [[nodiscard]] bool IsModelFrozen(ModelID modelId) const override;
    [[nodiscard]] bool IsModelResident(ModelID modelId) const override { return models.Contains(modelId); }
    [[nodiscard]] const CatalogTable<ModelID, CachedModelMetadata>& GetModelCache() const override { return modelCache; }
    [[nodiscard]] const CachedModelMetadata* GetModelMetadata(ModelID modelID) const override { return modelCache.Find(modelID); }

    PhysicsColliderHandle LoadSplineCollider(const SplineParams& params, bool bHollow) override;
    PhysicsColliderHandle LoadProceduralCollider(const ProceduralParams& params, const ProceduralRepeat& repeat = {}) override;
    PhysicsColliderHandle LoadModelCollider(ModelID sourceModelId, PhysicsColliderKind kind, uint32_t nodeIndex = ~0u) override;
    PhysicsColliderHandle LoadText3DCollider(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius, bool bPrecise) override;
    PhysicsColliderAsset* GetCollider(PhysicsColliderHandle handle) override;
    const PhysicsColliderAsset* GetCollider(PhysicsColliderHandle handle) const override;
    void UnloadCollider(PhysicsColliderHandle handle) override;
    [[nodiscard]] uint32_t GetActiveColliderCount() const override { return colliders.Count(); }

    [[nodiscard]] const CatalogTable<TextureID, CachedTextureMetadata>& GetTextureRegistry() const override { return textureRegistry; }
    [[nodiscard]] bool IsTextureLoaded(const TextureID textureId) const override { return textures.Contains(textureId); }
    [[nodiscard]] int32_t GetTextureBindlessIndex(const TextureID textureId) const override
    {
        const Texture* texture = textures.Get(textures.Find(textureId));
        if (texture == nullptr) { return -1; }
        return static_cast<int32_t>(texture->bindlessHandle.index);
    }
    [[nodiscard]] TextureID FindTextureByName(std::string_view name) const override { return FindByName(textureRegistry, name); }
    Texture* LoadTexture(TextureID textureId) override;
    Texture* LoadProceduralTexture(StringID pipelineId, uint32_t width, uint32_t height, VkFormat format, bool mipmapped, Texture::Origin origin, Core::InlineString<128> displayName = {}) override;
    void UnloadTexture(TextureID id) override;
    [[nodiscard]] uint32_t GetTextureInfoCount() const override { return static_cast<uint32_t>(textureRegistry.Size() + textures.Count()); }

    Sampler* LoadSampler(SamplerDesc& samplerDesc) override;
    void UnloadSampler(SamplerDesc& desc) override;

    [[nodiscard]] EnvironmentMapID FindCubemapByName(std::string_view name) const override { return FindByName(cubemapCache, name); }
    [[nodiscard]] bool IsCubemapLoaded(const EnvironmentMapID cubemapId) const override { return cubemaps.Contains(cubemapId); }
    CubemapHandle LoadCubemap(EnvironmentMapID cubemapId) override;
    Render::Cubemap* GetCubemap(CubemapHandle handle) override;
    const Render::Cubemap* GetCubemap(CubemapHandle handle) const override;
    void UnloadCubemap(CubemapHandle handle) override;
    [[nodiscard]] const CatalogTable<EnvironmentMapID, CachedCubemapMetadata>& GetCubemapCache() const override { return cubemapCache; }
    [[nodiscard]] const CachedCubemapMetadata* GetCubemapMetadata(EnvironmentMapID cubemapId) const override { return cubemapCache.Find(cubemapId); }

    TextureArrayHandle LoadTextureArray(TextureArrayID textureArrayId) override;
    Render::TextureArray* GetTextureArray(TextureArrayHandle handle) override;
    const Render::TextureArray* GetTextureArray(TextureArrayHandle handle) const override;
    void UnloadTextureArray(TextureArrayHandle handle) override;
    [[nodiscard]] const CachedTextureArrayMetadata* GetTextureArrayMetadata(TextureArrayID textureArrayId) const override { return textureArrayCache.Find(textureArrayId); }

    [[nodiscard]] const ShadowMapInfo* GetShadowMapInfo(uint64_t shadowId) const override { return shadowMapRegistry.Find(TextureArrayID{shadowId}); }
    TextureArrayHandle LoadShadowMap(uint64_t shadowId) override;

    CubemapHandle LoadProbe(ProbeID probeId) override;
    void UnloadProbe(CubemapHandle handle) override;
    [[nodiscard]] const ProbeInfo* GetProbeInfo(ProbeID probeId) const override { return probeRegistry.Find(probeId); }

    [[nodiscard]] FontID FindFontByName(std::string_view name) const override { return FindByName(fontCache, name); }
    FontHandle LoadFont(FontID id) override;
    void UnloadFont(FontHandle handle) override;
    [[nodiscard]] bool IsFontFrozen(FontID fontId) const override;
    [[nodiscard]] bool IsFontResident(FontID fontId) const override { return fonts.Contains(fontId); }
    Font* GetFont(FontHandle handle) override;
    const Font* GetFont(FontHandle handle) const override;
    [[nodiscard]] const WGlyphInfo* GetGlyph(FontHandle handle, uint32_t codepoint) const override;
    [[nodiscard]] const CatalogTable<FontID, CachedFontMetadata>& GetFontCache() const override { return fontCache; }
    [[nodiscard]] const CachedFontMetadata* GetFontMetadata(FontID fontId) const override { return fontCache.Find(fontId); }
    [[nodiscard]] uint32_t GetActiveFontCount() const override { return fonts.Count(); }

    [[nodiscard]] const CatalogTable<StringID, CachedSceneMetadata>& GetSceneCache() const override { return sceneCache; }
    [[nodiscard]] const CachedSceneMetadata* GetSceneMetadata(StringID sceneId) const override;
    [[nodiscard]] Core::Span<const Core::Path> GetSceneFolders() const override { return sceneFolders; }
    [[nodiscard]] const CatalogTable<StringID, CachedPrefabMetadata>& GetPrefabCache() const override { return prefabCache; }
    [[nodiscard]] const CachedPrefabMetadata* GetPrefabMetadata(StringID prefabId) const override;
    [[nodiscard]] const CatalogTable<StringID, CachedPlayMetadata>& GetPlayCache() const override { return playCache; }

public: // Exclusive-only
    void FreezeModel(ModelID modelId);
    void UnfreezeModel(ModelID modelId);
    void FreezeFont(FontID fontId);
    void UnfreezeFont(FontID fontId);

    bool ReloadTexture(TextureID textureId);
    bool ReloadProceduralTexture(TextureID textureId);

    /** Hot-reload: refreshes an already-loaded cubemap's runtime fields from cubemapCache metadata and re-requests the async load. No-op if the cubemap is not currently resident. */
    bool ReloadCubemap(EnvironmentMapID cubemapId);

    /** Hot-reload: re-reads an already-loaded texture array from textureArrayCache. No-op if it is not resident. */
    bool ReloadTextureArray(TextureArrayID textureArrayId);

    /**
     * Populate a caller-owned ArenaFixedMap with info for every known texture (catalog + loaded runtime procedurals).
     * The map must be pre-allocated with sufficient capacity. Existing entries are overwritten on collision.
     * @param out Arena-backed map to fill; capacity must be >= total texture count.
     */
    void GetAllTextureInfos(Core::ArenaFixedMap<TextureID, EditorTextureInfo>& out) const;

    [[nodiscard]] bool HasPendingLoads() const;

    void LogPendingLoads() const;

    ResolveLoadResult ResolveLoads(Core::FrameBuffer& stagingFrameBuffer);

    void KickOffRetires();

    /** Reclaims drained assets. A reclaimed model or font may have lifted a hot-reload freeze. */
    ResolveUnloadResult ResolveUnloads();

    /**
     * Scan for assets. Done once in constructor, but editor calls this frequently to gather generated assets.
     * @param loadedScenes stay registered when their file is missing
     */
    void Scan(Core::Span<const StringID> loadedScenes = {});

    void RegisterProceduralTextures();

    /** IDs whose on-disk content_version changed on the most recent Scan(); cleared at the top of every Scan() call. */
    [[nodiscard]] Core::Span<const ModelID> GetChangedModelIds() const { return {changedModelIds.Data(), changedModelIds.Size()}; }
    [[nodiscard]] Core::Span<const TextureID> GetChangedTextureIds() const { return {changedTextureIds.Data(), changedTextureIds.Size()}; }
    [[nodiscard]] Core::Span<const FontID> GetChangedFontIds() const { return {changedFontIds.Data(), changedFontIds.Size()}; }
    [[nodiscard]] Core::Span<const EnvironmentMapID> GetChangedEnvironmentMapIds() const { return {changedEnvironmentMapIds.Data(), changedEnvironmentMapIds.Size()}; }
    [[nodiscard]] Core::Span<const TextureArrayID> GetChangedTextureArrayIds() const { return {changedTextureArrayIds.Data(), changedTextureArrayIds.Size()}; }

    void RegisterScene(StringID sceneId, const Core::Path& source);

    void UpdateSceneCachePath(StringID sceneId, const Core::Path& path, uint32_t entityCount);

    bool DeleteScene(StringID sceneId);

    /**
     * Renames or moves a scene's file (if it has one) and the runs beside it; the scene's name becomes the new stem.
     * @return false if the destination exists or the move fails
     */
    bool MoveScene(StringID sceneId, const Core::Path& newSource);

    bool DeletePrefab(StringID prefabId);

    std::atomic<bool> bVerboseLogging{false};

private:
    Core::MemoryManager* memoryManager{};
    Engine::EngineContext* ctx;
    AssetLoad::AsyncAssetLoadManager* assetLoadManager;
    Render::ResourceManager* resourceManager;

    AssetPool<StaticModel, ModelID, MAX_LOADED_MODELS> models;

    Core::ConcurrentSlotPool<ModuleParams, MAX_LOADED_MODULE_MODELS> moduleParamsPool;
    Core::ConcurrentSlotPool<SplineParams, MAX_POOLED_SPLINE_PARAMS> splineParamsPool;
    Core::ConcurrentSlotPool<Text3DParams, MAX_POOLED_TEXT3D_PARAMS> text3DParamsPool;

    AssetPool<PhysicsColliderAsset, PhysicsColliderID, MAX_LOADED_COLLIDERS> colliders;
    AssetPool<Texture, TextureID, MAX_LOADED_TEXTURES> textures;
    AssetPool<Sampler, SamplerID, MAX_LOADED_SAMPLERS> samplers;
    AssetPool<Render::Cubemap, EnvironmentMapID, MAX_LOADED_CUBEMAPS> cubemaps;
    AssetPool<Render::TextureArray, TextureArrayID, MAX_LOADED_TEXTURE_ARRAYS> textureArrays;

    /** Batches per-tick counts into one "N <what>" line once activity has been idle ASSET_LOG_IDLE_SECONDS. */
    struct IdleLogCounter
    {
        int32_t pending{0};
        std::chrono::steady_clock::time_point lastActivity{};

        void Tick(int32_t thisTick, const char* what);
    };

    IdleLogCounter modelLoadLog{};
    IdleLogCounter textureLoadLog{};
    IdleLogCounter proceduralTextureLoadLog{};
    IdleLogCounter cubemapLoadLog{};
    IdleLogCounter samplerLoadLog{};

    AssetPool<Font, FontID, MAX_LOADED_FONTS> fonts;

    // Hot-reload freeze sets (editor-only): load resolve systems must not (re)acquire them until unfrozen.
    Core::InlineVector<ModelID, 16> frozenModelIds{};
    Core::InlineVector<FontID, 16> frozenFontIds{};

    IdleLogCounter fontLoadLog{};
    IdleLogCounter fontUnloadLog{};
    IdleLogCounter modelUnloadLog{};
    IdleLogCounter textureUnloadLog{};
    IdleLogCounter samplerUnloadLog{};

private: // Asset Registry
    template<typename Id, typename Entry>
    static Id FindByName(const CatalogTable<Id, Entry>& table, std::string_view name)
    {
        const StringID sid{name.data(), name.size()};
        if (table.IsNameAmbiguous(sid)) {
            LOG_WARN(Asset, "FindByName('{}'): name is shared by several files; returning one of them", name);
        }
        return table.FindByName(sid);
    }

    Texture* LoadDiskTexture(TextureID textureId);

    CatalogTable<ModelID, CachedModelMetadata> modelCache;
    CatalogTable<FontID, CachedFontMetadata> fontCache;
    CatalogTable<TextureID, CachedTextureMetadata> textureRegistry;
    CatalogTable<EnvironmentMapID, CachedCubemapMetadata> cubemapCache;
    CatalogTable<ProbeID, ProbeInfo> probeRegistry;
    CatalogTable<TextureArrayID, CachedTextureArrayMetadata> textureArrayCache;
    CatalogTable<TextureArrayID, ShadowMapInfo> shadowMapRegistry;
    CatalogTable<StringID, CachedSceneMetadata> sceneCache;
    CatalogTable<StringID, CachedPrefabMetadata> prefabCache;
    CatalogTable<StringID, CachedPlayMetadata> playCache;
    uint32_t scanGeneration{0};
    Core::InlineVector<Core::Path, 64> sceneFolders{};

    Core::Vector<ModelID> changedModelIds;
    Core::Vector<TextureID> changedTextureIds;
    Core::Vector<FontID> changedFontIds;
    Core::Vector<EnvironmentMapID> changedEnvironmentMapIds;
    Core::Vector<TextureArrayID> changedTextureArrayIds;

    struct DeferredTextureBindingRelease
    {
        Render::BindlessTextureHandle handle{};
        uint64_t releaseFrame{0};
        Render::AllocatedImage image{};
        Render::ImageView imageView{};
    };

    struct DeferredCubemapBindingRelease
    {
        Render::BindlessCubemapHandle handle{};
        uint64_t releaseFrame{0};
    };

    struct DeferredTextureArrayRelease
    {
        Render::BindlessTextureArrayHandle handle{};
        uint64_t releaseFrame{0};
        Render::AllocatedImage image{};
        Render::ImageView imageView{};
    };

    /** Old bindless slots orphaned by Reload*; released in ResolveUnloads once in-flight frames can no longer reference them. */
    Core::Vector<DeferredTextureBindingRelease> deferredTextureBindingReleases;
    Core::Vector<DeferredCubemapBindingRelease> deferredCubemapBindingReleases;
    Core::Vector<DeferredTextureArrayRelease> deferredTextureArrayReleases;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
