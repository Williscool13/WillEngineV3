//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H
#include <random>

#include "asset_manager_config.h"
#include "asset_manager_types.h"
#include "engine/core/model_id.h"
#include "core/sampler_id.h"
#include "engine/include/engine_context.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/containers/inline_map.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "core/containers/vector.h"
#include "engine/resources/sampler/sampler.h"
#include "render/types/cubemap_asset.h"
#include "resources/model/model_types.h"
#include "engine/resources/texture/texture.h"
#include "engine/resources/model/static_model.h"
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
};

class AssetManager
{
public:
    AssetManager(Core::MemoryManager& memoryManager, Core::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager);

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

    [[nodiscard]] uint32_t GetActiveModelCount()   const { return modelAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveTextureCount()  const { return textureAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveSamplerCount()  const { return samplerAllocator.GetCount(); }
    [[nodiscard]] uint32_t GetActiveCubemapCount()  const { return cubemapAllocator.GetCount(); }

    StaticModelHandle LoadModel(ModelID modelId);

    StaticModelHandle LoadProceduralModel(ProceduralParams& params);

    StaticModelHandle LoadSplineModel(const SplineParams& params);

    StaticModel* GetModel(StaticModelHandle handle);

    void UnloadModel(StaticModelHandle handle);

    struct CachedModelMetadata
    {
        Core::Path source;
        Core::InlineString<128> name{};
        uint32_t nodeCount{};
        uint32_t meshNodesCount{};
        Core::Vector<Node> nodes;
        ModelBounds bounds{};
    };

    const Core::FixedMap<ModelID, CachedModelMetadata>& GetModelCache() { return modelCache; }

    [[nodiscard]] const CachedModelMetadata* GetModelMetadata(ModelID modelID) const
    {
        return modelCache.Find(modelID);
    }


public: // Textures
    [[nodiscard]] TextureID FindTextureByName(std::string_view name) const
    {
        const StringID sid{name.data(), name.size()};
        const TextureID* found = textureNameToId.Find(sid);
        return found ? *found : TextureID::INVALID;
    }

    Texture* LoadTexture(TextureID textureId);

    void UnloadTexture(TextureID id);

    struct CachedTextureMetadata
    {
        Core::Path source;
        char name[WTEXTURE_NAME_LENGTH]{};
        uint32_t width{};
        uint32_t height{};
        uint32_t mipCount{};
        uint64_t dataOffset{};
        uint64_t dataSize{};
        uint64_t uncompressedSize{};
    };

    [[nodiscard]] const Core::FixedMap<StringID, TextureID>& GetTextureNameToId() const { return textureNameToId; }
    [[nodiscard]] const Core::FixedMap<TextureID, CachedTextureMetadata>& GetTextureCache() const { return textureCache; }

    [[nodiscard]] const CachedTextureMetadata* GetTextureMetadata(TextureID textureID) const
    {
        return textureCache.Find(textureID);
    }

public: // Samplers
    Sampler* LoadSampler(SamplerDesc& samplerDesc);

    void UnloadSampler(SamplerDesc& desc);

public: // Cubemaps
    CubemapHandle LoadCubemap(StringID cubemapId);

    Render::Cubemap* GetCubemap(CubemapHandle handle);

    void UnloadCubemap(CubemapHandle handle);

public: // Per-Tick calls
    ResolveLoadResult ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const;

    void ResolveUnloads();

    void Scan();

public:
    OffsetAllocator::Allocator& GetJointMatrixAllocator()
    {
        return jointMatrixAllocator;
    }

private:
    Core::MemoryManager* memoryManager{};
    Core::EngineContext* ctx;
    AssetLoad::AsyncAssetLoadManager* assetLoadManager;
    Render::ResourceManager* resourceManager;

    // todo: figure out whats happening to this guy
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    Core::InlineMap<ModelID, StaticModelHandle, 4096> modelIdToHandle;
    Core::HandleAllocator<StaticModel, MAX_LOADED_MODELS> modelAllocator;
    Core::Array<StaticModel, MAX_LOADED_MODELS> models;

    Core::HandleAllocator<Texture, MAX_LOADED_TEXTURES> textureAllocator;
    Core::Array<Texture, MAX_LOADED_TEXTURES> textures{};
    Core::InlineMap<TextureID, TextureHandle, 4096> textureIdToHandle;

    Core::HandleAllocator<Sampler, MAX_LOADED_SAMPLERS> samplerAllocator;
    Core::Array<Sampler, MAX_LOADED_SAMPLERS> samplers{};
    Core::InlineMap<SamplerID, SamplerHandle, 256> samplerIdToHandle;

    Core::HandleAllocator<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemapAllocator;
    Core::Array<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemaps{};
    Core::InlineMap<StringID, CubemapHandle, 512> cubemapIdToHandle;

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
    struct CachedCubemapMetadata
    {
        Core::Path source;
    };

    Core::FixedMap<StringID, ModelID> modelNameToId;
    Core::FixedMap<ModelID, CachedModelMetadata> modelCache;

    Core::FixedMap<StringID, TextureID> textureNameToId;
    Core::FixedMap<TextureID, CachedTextureMetadata> textureCache;

    Core::FixedMap<StringID, CachedCubemapMetadata> cubemapCache;

    Core::FixedMap<StringID, CachedSceneMetadata> sceneCache;
    Core::FixedMap<StringID, CachedPrefabMetadata> prefabCache;

    /**
     * For (almost 100% chance) unique procedural shapes
     */
    std::mt19937_64 proceduralModelIdRng{std::random_device{}()};
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
