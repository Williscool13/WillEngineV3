//
// Created by William on 2026-10-30.
//

#ifndef WILL_ENGINE_ASSET_LOADER_H
#define WILL_ENGINE_ASSET_LOADER_H

#include <string_view>

#include "asset_manager_types.h"
#include "catalog_table.h"
#include "core/containers/arena_fixed_map.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "core/containers/span.h"
#include "core/sampler_id.h"
#include "engine/components/render_components.h"
#include "engine/core/environment_map_id.h"
#include "engine/core/font_id.h"
#include "engine/core/model_id.h"
#include "engine/core/physics_collider_id.h"
#include "engine/core/probe_id.h"
#include "engine/core/texture_array_id.h"
#include "engine/resources/environment_map/probe_format.h"
#include "engine/resources/font/font.h"
#include "engine/resources/model/static_model.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/resources/sampler/sampler.h"
#include "engine/resources/shadow_map/shadow_map_format.h"
#include "engine/resources/texture/texture.h"
#include "render/types/cubemap_asset.h"
#include "render/types/texture_array_asset.h"
#include "resources/model/model_types.h"

namespace Engine
{
/** The part of AssetManager safe from any thread. Reached through registry.Assets() with no access declaration, so everything added here must be thread safe. */
class IAssetLoader
{
public: // Models
    struct CachedModelMetadata
    {
        Core::Path source;
        Core::InlineString<128> name{};
        uint32_t nodeCount{};
        uint32_t meshNodesCount{};
        Core::HeapArray<Node> nodes;
        ModelBounds bounds{};
        uint64_t contentVersion{0};
        uint64_t uncompressedBodySize{0};
    };

    [[nodiscard]] virtual ModelID FindModelByName(std::string_view name) const = 0;

    [[nodiscard]] virtual uint32_t GetActiveModelCount() const = 0;
    [[nodiscard]] virtual uint32_t GetActiveTextureCount() const = 0;
    [[nodiscard]] virtual uint32_t GetActiveSamplerCount() const = 0;
    [[nodiscard]] virtual uint32_t GetActiveCubemapCount() const = 0;
    [[nodiscard]] virtual uint32_t GetActiveTextureArrayCount() const = 0;

    virtual StaticModelHandle LoadModel(ModelID modelId) = 0;

    virtual StaticModelHandle LoadProceduralModel(const ProceduralParams& params, const ProceduralRepeat& repeat = {}) = 0;

    virtual StaticModelHandle LoadSplineModel(const SplineParams& params) = 0;

    virtual StaticModelHandle LoadModuleModel(const ModuleParams& params) = 0;

    /**
     * Loads (or dedups) an extruded 3D-text model, resolving the font by id. Dedup key is the identity (font + text + depth + flatness + tracking + scale + smoothNormals).
     * The model holds a font ref until it finalizes so the generation worker can read the glyph contours.
     */
    virtual StaticModelHandle LoadText3DModel(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius) = 0;

    virtual StaticModel* GetModel(StaticModelHandle handle) = 0;
    virtual const StaticModel* GetModel(StaticModelHandle handle) const = 0;

    virtual void UnloadModel(StaticModelHandle handle) = 0;

    /** Hot-reload: while frozen, the load resolve systems skip (re)acquiring this model so it can drain and retire. */
    [[nodiscard]] virtual bool IsModelFrozen(ModelID modelId) const = 0;
    [[nodiscard]] virtual bool IsModelResident(ModelID modelId) const = 0;

    [[nodiscard]] virtual const CatalogTable<ModelID, CachedModelMetadata>& GetModelCache() const = 0;
    [[nodiscard]] virtual const CachedModelMetadata* GetModelMetadata(ModelID modelID) const = 0;

public: // Physics colliders (CPU-only, analytic)
    /**
     * Loads (or dedups) a Compound collider built analytically from a spline. Keyed by hash(spline) x kind.
     * @param params
     * @param bHollow a TriangleMesh of the open tube surface instead; ignored unless CanBuildHollowSplineCollider
     * @return
     */
    virtual PhysicsColliderHandle LoadSplineCollider(const SplineParams& params, bool bHollow) = 0;

    /**
     * Loads (or dedups) a collider for a procedural shape. Analytic types (primitive/compound) pick their own kind; the non-analytic "exotic" types (Bowl/CurvedRamp/Klein/Trefoil) generate + simplify geometry into a concave TriangleMesh (they are Static/Kinematic-only, never dynamic). Keyed by hash(params).
     */
    virtual PhysicsColliderHandle LoadProceduralCollider(const ProceduralParams& params, const ProceduralRepeat& repeat = {}) = 0;

    /**
     * Loads (or dedups) a collider read from an imported model's .wsmesh geometry (CPU-only, simplified). ConvexHull for dynamic bodies, TriangleMesh for static. Keyed by hash(modelId, nodeIndex) x kind; freeze-gate on the source ModelID at the call site.
     * @param sourceModelId
     * @param kind
     * @param nodeIndex ~0u for the whole model; otherwise that node and its descendants, in the node's local space
     * @return
     */
    virtual PhysicsColliderHandle LoadModelCollider(ModelID sourceModelId, PhysicsColliderKind kind, uint32_t nodeIndex = ~0u) = 0;

    /**
     * Loads (or dedups) a collider for extruded 3D text: one Box per glyph from the glyph plane bounds (Compound), or with @p bPrecise, the exact extruded glyph mesh simplified into a concave TriangleMesh. Takes a generation-scoped font ref (released when the collider finalizes); freeze-gate on the FontID at the call site.
     */
    virtual PhysicsColliderHandle LoadText3DCollider(FontID fontId, const Core::InlineString<256>& text, float depth, float flatness, float tracking, float scale, bool bSmoothNormals, Text3DAlign align, Text3DAnchor anchor, float wrapWidth, float bendRadius, bool bPrecise) = 0;

    virtual PhysicsColliderAsset* GetCollider(PhysicsColliderHandle handle) = 0;
    virtual const PhysicsColliderAsset* GetCollider(PhysicsColliderHandle handle) const = 0;

    virtual void UnloadCollider(PhysicsColliderHandle handle) = 0;

    [[nodiscard]] virtual uint32_t GetActiveColliderCount() const = 0;

public: // Textures
    /** A disk texture, or (origin StaticProcedural) one registered at startup and generated by a compute pipeline. */
    struct CachedTextureMetadata
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
        uint64_t contentVersion{0};
        bool bUngenerated{false};
        TextureCategory category{TextureCategory::Standalone};
        Texture::Origin origin{Texture::Origin::Disk};
        StringID pipelineId{};
        VkFormat format{};
        bool bMipmapped{false};
    };

    struct EditorTextureInfo
    {
        TextureID id;
        Core::InlineString<128> name;
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
        TextureCategory category{TextureCategory::Standalone};
    };

    [[nodiscard]] virtual const CatalogTable<TextureID, CachedTextureMetadata>& GetTextureRegistry() const = 0;

    [[nodiscard]] virtual bool IsTextureLoaded(TextureID textureId) const = 0;

    [[nodiscard]] virtual int32_t GetTextureBindlessIndex(TextureID textureId) const = 0;

    [[nodiscard]] virtual TextureID FindTextureByName(std::string_view name) const = 0;

    virtual Texture* LoadTexture(TextureID textureId) = 0;

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
    virtual Texture* LoadProceduralTexture(StringID pipelineId, uint32_t width, uint32_t height, VkFormat format, bool mipmapped, Texture::Origin origin, Core::InlineString<128> displayName = {}) = 0;

    virtual void UnloadTexture(TextureID id) = 0;

    /** @return Upper bound on the number of textures GetAllTextureInfos will emit; use for map capacity. */
    [[nodiscard]] virtual uint32_t GetTextureInfoCount() const = 0;

public: // Samplers
    virtual Sampler* LoadSampler(SamplerDesc& samplerDesc) = 0;

    virtual void UnloadSampler(SamplerDesc& desc) = 0;

public: // Cubemaps
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
        uint64_t contentVersion{0};
    };

    [[nodiscard]] virtual EnvironmentMapID FindCubemapByName(std::string_view name) const = 0;

    [[nodiscard]] virtual bool IsCubemapLoaded(EnvironmentMapID cubemapId) const = 0;

    virtual CubemapHandle LoadCubemap(EnvironmentMapID cubemapId) = 0;

    virtual Render::Cubemap* GetCubemap(CubemapHandle handle) = 0;
    virtual const Render::Cubemap* GetCubemap(CubemapHandle handle) const = 0;

    virtual void UnloadCubemap(CubemapHandle handle) = 0;

    [[nodiscard]] virtual const CatalogTable<EnvironmentMapID, CachedCubemapMetadata>& GetCubemapCache() const = 0;

    [[nodiscard]] virtual const CachedCubemapMetadata* GetCubemapMetadata(EnvironmentMapID cubemapId) const = 0;

public: // Texture arrays
    struct CachedTextureArrayMetadata
    {
        Core::Path source;
        Core::InlineString<128> name;
        uint64_t dataOffset{};
        uint64_t dataSize{};
        uint64_t uncompressedSize{};
        CompressionType compressionType{CompressionType::Zstd};
        uint64_t contentVersion{0};
    };

    virtual TextureArrayHandle LoadTextureArray(TextureArrayID textureArrayId) = 0;

    virtual Render::TextureArray* GetTextureArray(TextureArrayHandle handle) = 0;
    virtual const Render::TextureArray* GetTextureArray(TextureArrayHandle handle) const = 0;

    virtual void UnloadTextureArray(TextureArrayHandle handle) = 0;

    [[nodiscard]] virtual const CachedTextureArrayMetadata* GetTextureArrayMetadata(TextureArrayID textureArrayId) const = 0;

public: // Baked shadow maps
    struct ShadowMapInfo
    {
        Core::Path source;
        uint32_t resolution{0};
        ShadowBakeKey key{};
        uint64_t contentVersion{0};
    };

    [[nodiscard]] virtual const ShadowMapInfo* GetShadowMapInfo(uint64_t shadowId) const = 0;

    virtual TextureArrayHandle LoadShadowMap(uint64_t shadowId) = 0;

public: // Reflection probes
    struct ProbeInfo
    {
        Core::Path source;
        EnvironmentMapID environmentMapId{};
        uint32_t resolution{};
        ProbeBakeSnapshot snapshot{};
        uint64_t contentVersion{0};
        float radianceScale{1.0f};
    };

    /** Resolves a probe to its runtime cubemap and loads it through the shared cubemap path (keyed by the probe's EnvironmentMapID). @returns INVALID if the probe is not in the registry. */
    virtual CubemapHandle LoadProbe(ProbeID probeId) = 0;

    /** Releases a probe cubemap handle acquired from LoadProbe. */
    virtual void UnloadProbe(CubemapHandle handle) = 0;

    /** @returns the cached probe registry entry (path + EnvironmentMapID + resolution + bake snapshot), or null if not registered. */
    [[nodiscard]] virtual const ProbeInfo* GetProbeInfo(ProbeID probeId) const = 0;

public: // Fonts
    struct CachedFontMetadata
    {
        Core::Path source{};
        Core::InlineString<128> name{};
        WFontHeader header{};
        uint64_t contentVersion{0};
    };

    [[nodiscard]] virtual FontID FindFontByName(std::string_view name) const = 0;

    virtual FontHandle LoadFont(FontID id) = 0;

    virtual void UnloadFont(FontHandle handle) = 0;

    /** Hot-reload: while frozen, the load resolve systems skip (re)acquiring this font so it can drain and retire. */
    [[nodiscard]] virtual bool IsFontFrozen(FontID fontId) const = 0;
    [[nodiscard]] virtual bool IsFontResident(FontID fontId) const = 0;

    virtual Font* GetFont(FontHandle handle) = 0;
    virtual const Font* GetFont(FontHandle handle) const = 0;

    /** Returns the glyph info for the given codepoint, or nullptr if not found or not loaded. */
    [[nodiscard]] virtual const WGlyphInfo* GetGlyph(FontHandle handle, uint32_t codepoint) const = 0;

    [[nodiscard]] virtual const CatalogTable<FontID, CachedFontMetadata>& GetFontCache() const = 0;
    [[nodiscard]] virtual const CachedFontMetadata* GetFontMetadata(FontID fontId) const = 0;
    [[nodiscard]] virtual uint32_t GetActiveFontCount() const = 0;

public: // Scenes
    struct CachedSceneMetadata
    {
        Core::Path source;
        Core::InlineString<128> sceneName{};
        uint32_t entityCount{};
        uint64_t contentVersion{0};
        /** No file on disk; kept registered only while loaded. */
        bool bUnsaved{false};
    };

    [[nodiscard]] virtual const CatalogTable<StringID, CachedSceneMetadata>& GetSceneCache() const = 0;

    [[nodiscard]] virtual const CachedSceneMetadata* GetSceneMetadata(StringID sceneId) const = 0;

    /** Every directory under the scenes root, as of the last scene scan. */
    [[nodiscard]] virtual Core::Span<const Core::Path> GetSceneFolders() const = 0;

public: // Prefabs
    struct CachedPrefabMetadata
    {
        Core::Path source;
        Core::InlineString<128> prefabName{};
        uint32_t componentCount{};
        uint64_t contentVersion{0};
    };

    [[nodiscard]] virtual const CatalogTable<StringID, CachedPrefabMetadata>& GetPrefabCache() const = 0;

    [[nodiscard]] virtual const CachedPrefabMetadata* GetPrefabMetadata(StringID prefabId) const = 0;

public: // Runs (.wplay)
    struct CachedPlayMetadata
    {
        Core::Path source;
        Core::InlineString<128> name{};
        StringID sceneId{};
        uint32_t eventCount{};
        uint64_t contentVersion{0};
    };

    [[nodiscard]] virtual const CatalogTable<StringID, CachedPlayMetadata>& GetPlayCache() const = 0;

protected:
    ~IAssetLoader() = default;
};
} // Engine

#endif //WILL_ENGINE_ASSET_LOADER_H
