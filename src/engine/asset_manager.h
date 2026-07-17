//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H

#include "asset_manager_config.h"
#include "asset_manager_types.h"
#include "engine/core/environment_map_id.h"
#include "engine/core/font_id.h"
#include "engine/core/model_id.h"
#include "engine/core/physics_collider_id.h"
#include "core/sampler_id.h"
#include "engine/include/engine_context.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/containers/arena_fixed_map.h"
#include "core/containers/inline_map.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "core/containers/vector.h"
#include "engine/resources/sampler/sampler.h"
#include "render/types/cubemap_asset.h"
#include "resources/model/model_types.h"
#include "engine/resources/font/font.h"
#include "engine/resources/texture/texture.h"
#include "engine/resources/model/static_model.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "game/components/render_components.h"

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
    int32_t samplerLoadedCount{0};
    int32_t fontLoadedCount{0};
};

class AssetManager
{
public:
    AssetManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager);

    ~AssetManager();

    AssetManager(const AssetManager&) = delete;

    AssetManager& operator=(const AssetManager&) = delete;

    AssetManager(AssetManager&&) = delete;

    AssetManager& operator=(AssetManager&&) = delete;

public: // Models
    [[nodiscard]] ModelID FindModelByName(std::string_view name) const
    {
        const StringID sid{name.data(), name.size()};
        const ModelID* found = modelNameToId.Find(sid);
        return found ? *found : ModelID::INVALID;
    }

    [[nodiscard]] uint32_t GetActiveModelCount() const { return modelAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveTextureCount() const { return textureAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveSamplerCount() const { return samplerAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveCubemapCount() const { return cubemapAllocator.GetCount(); }

    StaticModelHandle LoadModel(ModelID modelId);

    StaticModelHandle LoadProceduralModel(ProceduralParams& params);

    StaticModelHandle LoadSplineModel(const SplineParams& params);

    /**
     * Loads (or dedups) an extruded 3D-text model, resolving the font by id. Dedup key is the identity (font + text + depth + flatness + tracking + scale + smoothNormals).
     * The model holds a font ref until it finalizes so the generation worker can read the glyph contours.
     */
    StaticModelHandle LoadText3DModel(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor);

    StaticModel* GetModel(StaticModelHandle handle);

    void UnloadModel(StaticModelHandle handle);

    /** Hot-reload: while frozen, the load resolve systems skip (re)acquiring this model so it can drain and retire. */
    void FreezeModel(ModelID modelId);
    void UnfreezeModel(ModelID modelId);
    [[nodiscard]] bool IsModelFrozen(ModelID modelId) const;
    [[nodiscard]] bool IsModelResident(ModelID modelId) const { return modelIdToHandle.Contains(modelId); }

    struct CachedModelMetadata
    {
        Core::Path source;
        Core::InlineString<128> name{};
        uint32_t nodeCount{};
        uint32_t meshNodesCount{};
        Core::HeapArray<Node> nodes;
        ModelBounds bounds{};
    };

    const Core::FixedMap<ModelID, CachedModelMetadata>& GetModelCache() { return modelCache; }

public: // Physics colliders (CPU-only, analytic)
    /**
     * Loads (or dedups) a Compound collider built analytically from a spline. Keyed by hash(spline) x kind.
     * @param params
     * @return
     */
    PhysicsColliderHandle LoadSplineCollider(const SplineParams& params);


    /**
     * Loads (or dedups) a collider for a procedural shape. Analytic types (primitive/compound) pick their own kind; the non-analytic "exotic" types (Bowl/CurvedRamp/Klein/Trefoil) generate + simplify geometry into a concave TriangleMesh (they are Static/Kinematic-only, never dynamic). Keyed by hash(params).
     */
    PhysicsColliderHandle LoadProceduralCollider(const ProceduralParams& params);

    /**
     * Loads (or dedups) a collider read from an imported model's .wsmesh geometry (CPU-only, simplified). ConvexHull for dynamic bodies, TriangleMesh for static. Keyed by hash(modelId) x kind; freeze-gate on the source ModelID at the call site.
     * @param sourceModelId
     * @param kind
     * @return
     */
    PhysicsColliderHandle LoadModelCollider(Engine::ModelID sourceModelId, PhysicsColliderKind kind);

    /**
     * Loads (or dedups) a collider for extruded 3D text: one Box per glyph from the glyph plane bounds (Compound), or with @p bPrecise, the exact extruded glyph mesh simplified into a concave TriangleMesh. Takes a generation-scoped font ref (released when the collider finalizes); freeze-gate on the FontID at the call site.
     */
    PhysicsColliderHandle LoadText3DCollider(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, bool bPrecise);

    PhysicsColliderAsset* GetCollider(PhysicsColliderHandle handle);

    void UnloadCollider(PhysicsColliderHandle handle);

    [[nodiscard]] uint32_t GetActiveColliderCount() const { return colliderAllocator.GetCount(); }

    [[nodiscard]] const CachedModelMetadata* GetModelMetadata(ModelID modelID) const
    {
        return modelCache.Find(modelID);
    }

public: // Textures
    struct DiskTextureDesc
    {
        Core::Path source;
        Core::InlineString<128> name;
        uint32_t width{};
        uint32_t height{};
        uint32_t mipCount{};
        uint64_t dataOffset{};
        uint64_t dataSize{};
        uint64_t uncompressedSize{};
        CompressionType compressionType{DEFAULT_TEXTURE_COMPRESSION};
    };

    struct StaticProceduralDesc
    {
        StringID pipelineId;
        uint32_t width{};
        uint32_t height{};
        VkFormat format{};
        bool mipmapped{false};
        Core::InlineString<128> name;
    };

    struct EditorTextureInfo
    {
        TextureID id;
        Core::InlineString<128> name;
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
    };

    bool IsTextureLoaded(const TextureID textureId) const
    {
        return textureIdToHandle.Contains(textureId);
    }

    [[nodiscard]] TextureID FindTextureByName(std::string_view name) const
    {
        const StringID sid{name.data(), name.size()};
        const TextureID* found = textureNameToId.Find(sid);
        return found ? *found : TextureID::INVALID;
    }

    Texture* LoadTexture(TextureID textureId);

    bool ReloadTexture(TextureID textureId);

    /**
     * Load a procedurally generated texture. If already loaded, returns the existing instance (deduplication by pipelineId).
     * Pre-reserves a bindless slot and enqueues a compute dispatch via AsyncAssetLoadManager.
     * @param pipelineId Compute pipeline that generates the texture; also serves as the TextureID.
     * @param width Texture width in pixels.
     * @param height Texture height in pixels.
     * @param format VkFormat; must support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT.
     * @param mipmapped Generate full mip chain after dispatch.
     * @param origin StaticProcedural if loaded via registry, RuntimeProcedural if called explicitly.
     * @param displayName Optional display name override; auto-generated from textureId if null.
     * @return Pointer to the Texture (LoadState::Loading until generation completes), or nullptr on failure.
     */
    Texture* LoadProceduralTexture(StringID pipelineId, uint32_t width, uint32_t height, VkFormat format, bool mipmapped, Texture::Origin origin, Core::InlineString<128> displayName = {});

    void UnloadTexture(TextureID id);

    /** @return Upper bound on the number of textures GetAllTextureInfos will emit; use for map capacity. */
    [[nodiscard]] uint32_t GetTextureInfoCount() const { return static_cast<uint32_t>(textureRegistry.Size() + staticProceduralRegistry.Size() + textureIdToHandle.Size()); }

    /**
     * Populate a caller-owned ArenaFixedMap with info for every known texture (disk + static procedural + loaded runtime procedurals).
     * The map must be pre-allocated with sufficient capacity. Existing entries are overwritten on collision.
     * @param out Arena-backed map to fill; capacity must be >= total texture count.
     */
    void GetAllTextureInfos(Core::ArenaFixedMap<TextureID, EditorTextureInfo>& out) const;

public: // Samplers
    Sampler* LoadSampler(SamplerDesc& samplerDesc);

    void UnloadSampler(SamplerDesc& desc);

public: // Cubemaps
    [[nodiscard]] EnvironmentMapID FindCubemapByName(std::string_view name) const
    {
        const StringID sid{name.data(), name.size()};
        const EnvironmentMapID* found = cubemapNameToId.Find(sid);
        return found ? *found : EnvironmentMapID::INVALID;
    }

    CubemapHandle LoadCubemap(EnvironmentMapID cubemapId);

    Render::Cubemap* GetCubemap(CubemapHandle handle);

    void UnloadCubemap(CubemapHandle handle);

    struct CachedCubemapMetadata
    {
        Core::Path source;
        Core::InlineString<128> name;
        uint32_t width{};
        uint32_t height{};
        uint32_t mipCount{};
        uint64_t dataOffset{};
        uint64_t dataSize{};
        uint64_t uncompressedSize{};
        CompressionType compressionType{DEFAULT_ENV_MAP_COMPRESSION};
    };

    [[nodiscard]] const Core::FixedMap<StringID, EnvironmentMapID>& GetCubemapNameToId() const { return cubemapNameToId; }
    [[nodiscard]] const Core::FixedMap<EnvironmentMapID, CachedCubemapMetadata>& GetCubemapCache() const { return cubemapCache; }

    [[nodiscard]] const CachedCubemapMetadata* GetCubemapMetadata(EnvironmentMapID cubemapId) const
    {
        return cubemapCache.Find(cubemapId);
    }

public: // Fonts
    [[nodiscard]] FontID FindFontByName(std::string_view name) const
    {
        const StringID sid{name.data(), name.size()};
        const FontID* found = fontNameToId.Find(sid);
        return found ? *found : FontID::INVALID;
    }

    FontHandle LoadFont(FontID id);

    void UnloadFont(FontHandle handle);

    /** Hot-reload: while frozen, the load resolve systems skip (re)acquiring this font so it can drain and retire. */
    void FreezeFont(FontID fontId);
    void UnfreezeFont(FontID fontId);
    [[nodiscard]] bool IsFontFrozen(FontID fontId) const;
    [[nodiscard]] bool IsFontResident(FontID fontId) const { return fontIdToHandle.Contains(fontId); }

    Font* GetFont(FontHandle handle);

    /** Returns the glyph info for the given codepoint, or nullptr if not found or not loaded. */
    [[nodiscard]] const WGlyphInfo* GetGlyph(FontHandle handle, uint32_t codepoint) const;

    struct CachedFontMetadata
    {
        Core::Path source{};
        Core::InlineString<128> name{};
        WFontHeader header{};
    };

    [[nodiscard]] const Core::FixedMap<StringID, FontID>& GetFontNameToId() const { return fontNameToId; }
    [[nodiscard]] const Core::FixedMap<FontID, CachedFontMetadata>& GetFontCache() const { return fontCache; }
    [[nodiscard]] const CachedFontMetadata* GetFontMetadata(FontID fontId) const { return fontCache.Find(fontId); }
    [[nodiscard]] uint32_t GetActiveFontCount() const { return fontAllocator.GetCount(); }

public: // Per-Tick calls
    ResolveLoadResult ResolveLoads(Core::FrameBuffer& stagingFrameBuffer);

    void KickOffRetires();

    /** Reclaims drained assets. Returns true if a model/font was reclaimed (which may have lifted a hot-reload freeze). */
    bool ResolveUnloads();

    /**
     * Scan for assets. Done once in constructor, but editor calls this frequently to gather generated assets.
     */
    void Scan();

    void RegisterProceduralTextures();

public:
    OffsetAllocator::Allocator& GetJointMatrixAllocator()
    {
        return jointMatrixAllocator;
    }

    std::atomic<bool> bVerboseLogging{false};

private:
    Core::MemoryManager* memoryManager{};
    Engine::EngineContext* ctx;
    AssetLoad::AsyncAssetLoadManager* assetLoadManager;
    Render::ResourceManager* resourceManager;

    // todo: figure out whats happening to this guy
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    Core::InlineMap<ModelID, StaticModelHandle, 4096> modelIdToHandle;
    Core::HandleAllocator<StaticModel, MAX_LOADED_MODELS> modelAllocator;
    Core::Array<StaticModel, MAX_LOADED_MODELS> models;

    Core::InlineMap<PhysicsColliderID, PhysicsColliderHandle, 4096> colliderIdToHandle;
    Core::HandleAllocator<PhysicsColliderAsset, MAX_LOADED_COLLIDERS> colliderAllocator;
    Core::Array<PhysicsColliderAsset, MAX_LOADED_COLLIDERS> colliders;

    Core::HandleAllocator<Texture, MAX_LOADED_TEXTURES> textureAllocator;
    Core::Array<Texture, MAX_LOADED_TEXTURES> textures{};
    Core::InlineMap<TextureID, TextureHandle, 4096> textureIdToHandle;

    Core::HandleAllocator<Sampler, MAX_LOADED_SAMPLERS> samplerAllocator;
    Core::Array<Sampler, MAX_LOADED_SAMPLERS> samplers{};
    Core::InlineMap<SamplerID, SamplerHandle, 256> samplerIdToHandle;

    Core::HandleAllocator<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemapAllocator;
    Core::Array<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemaps{};
    Core::InlineMap<EnvironmentMapID, CubemapHandle, 512> cubemapIdToHandle;

    int32_t pendingModelLogCount{0};
    std::chrono::steady_clock::time_point modelLastActivity{};

    int32_t pendingTextureLogCount{0};
    std::chrono::steady_clock::time_point textureLastActivity{};

    int32_t pendingProceduralTextureLogCount{0};
    std::chrono::steady_clock::time_point proceduralTextureLastActivity{};

    int32_t pendingCubemapLogCount{0};
    std::chrono::steady_clock::time_point cubemapLastActivity{};

    int32_t pendingSamplerLogCount{0};
    std::chrono::steady_clock::time_point samplerLastActivity{};

    Core::HandleAllocator<Font, MAX_LOADED_FONTS> fontAllocator;
    Core::Array<Font, MAX_LOADED_FONTS> fonts{};
    Core::InlineMap<FontID, FontHandle, MAX_LOADED_FONTS> fontIdToHandle;

    // Hot-reload freeze sets (editor-only): load resolve systems must not (re)acquire them until unfrozen.
    Core::InlineVector<ModelID, 16> frozenModelIds{};
    Core::InlineVector<FontID, 16> frozenFontIds{};

    int32_t pendingFontLogCount{0};
    std::chrono::steady_clock::time_point fontLastActivity{};

    int32_t pendingFontUnloadLogCount{0};
    std::chrono::steady_clock::time_point fontUnloadLastActivity{};

    int32_t pendingModelUnloadLogCount{0};
    std::chrono::steady_clock::time_point modelUnloadLastActivity{};

    int32_t pendingTextureUnloadLogCount{0};
    std::chrono::steady_clock::time_point textureUnloadLastActivity{};

    int32_t pendingSamplerUnloadLogCount{0};
    std::chrono::steady_clock::time_point samplerUnloadLastActivity{};

public: // Scenes
    struct CachedSceneMetadata
    {
        Core::Path source;
        Core::InlineString<128> sceneName{};
        uint32_t entityCount{};
    };

    const Core::FixedMap<StringID, CachedSceneMetadata>& GetSceneCache() { return sceneCache; }

    [[nodiscard]] const CachedSceneMetadata* GetSceneMetadata(StringID sceneId) const;

    void RegisterScene(StringID sceneId, const char* sceneName);

    void UpdateSceneCachePath(StringID sceneId, const Core::Path& path, uint32_t entityCount);

    bool DeleteScene(StringID sceneId);

public: // Prefabs
    struct CachedPrefabMetadata
    {
        Core::Path source;
        Core::InlineString<128> prefabName{};
        uint32_t componentCount{};
    };

    const Core::FixedMap<StringID, CachedPrefabMetadata>& GetPrefabCache() { return prefabCache; }

    [[nodiscard]] const CachedPrefabMetadata* GetPrefabMetadata(StringID prefabId) const;

    bool DeletePrefab(StringID prefabId);

private: // Asset Registry
    Core::FixedMap<StringID, ModelID> modelNameToId;
    Core::FixedMap<ModelID, CachedModelMetadata> modelCache;

    Texture* LoadDiskTexture(TextureID textureId);

    Core::FixedMap<StringID, FontID> fontNameToId;
    Core::FixedMap<FontID, CachedFontMetadata> fontCache;

    Core::FixedMap<StringID, TextureID> textureNameToId;
    Core::FixedMap<TextureID, DiskTextureDesc> textureRegistry;
    Core::FixedMap<TextureID, StaticProceduralDesc> staticProceduralRegistry;

    Core::FixedMap<StringID, EnvironmentMapID> cubemapNameToId;
    Core::FixedMap<EnvironmentMapID, CachedCubemapMetadata> cubemapCache;

    Core::FixedMap<StringID, CachedSceneMetadata> sceneCache;
    Core::FixedMap<StringID, CachedPrefabMetadata> prefabCache;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
