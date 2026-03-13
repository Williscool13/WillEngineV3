//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H
#include <filesystem>
#include <unordered_map>

#include "asset_manager_config.h"
#include "asset_manager_types.h"
#include "core/sampler_id.h"
#include "core/include/engine_context.h"
#include "core/allocators/handle_allocator.h"
#include "engine/resources/sampler/sampler.h"
#include "render/types/cubemap_asset.h"
#include "resources/model/model_format.h"
#include "resources/model/model_types.h"
#include "engine/resources/texture/texture.h"
#include "engine/resources/model/static_model.h"

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
    AssetManager(Core::EngineContext* ctx, AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager);

    ~AssetManager();

    AssetManager(const AssetManager&) = delete;

    AssetManager& operator=(const AssetManager&) = delete;

    AssetManager(AssetManager&&) = delete;

    AssetManager& operator=(AssetManager&&) = delete;

public: // Models
    StaticModelHandle LoadModel(StringID modelId);

    StaticModel* GetModel(StaticModelHandle handle);

    void UnloadModel(StaticModelHandle handle);

    const std::unordered_map<StringID, std::filesystem::path>& GetModelRegistry() { return modelRegistry; }

    struct CachedModelMetadata
    {
        uint32_t nodeCount{};
        uint32_t meshNodesCount{};
        std::vector<Node> nodes;
    };

    [[nodiscard]] const CachedModelMetadata* GetModelMetadata(StringID modelId) const;


public: // Textures
    Texture* LoadTexture(TextureID textureId);

    void UnloadTexture(TextureID id);

    [[nodiscard]] TextureID FindTextureByName(std::string_view name) const;

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
    Core::EngineContext* ctx;
    AssetLoad::AsyncAssetLoadManager* assetLoadManager;
    Render::ResourceManager* resourceManager;

    // todo: figure out whats happening to this guy
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    std::unordered_map<StringID, StaticModelHandle> modelIdToHandle;
    Core::HandleAllocator<StaticModel, MAX_LOADED_MODELS> modelAllocator;
    std::array<StaticModel, MAX_LOADED_MODELS> models;

    Core::HandleAllocator<Texture, MAX_LOADED_TEXTURES> textureAllocator;
    std::array<Texture, MAX_LOADED_TEXTURES> textures{};
    std::unordered_map<TextureID, TextureHandle> textureIdToHandle;

    Core::HandleAllocator<Sampler, MAX_LOADED_SAMPLERS> samplerAllocator;
    std::array<Sampler, MAX_LOADED_SAMPLERS> samplers{};
    std::unordered_map<SamplerID, SamplerHandle> samplerIdToHandle;

    Core::HandleAllocator<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemapAllocator;
    std::array<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemaps{};
    std::unordered_map<StringID, CubemapHandle> cubemapIdToHandle;

public: // Scenes
    const std::unordered_map<StringID, std::filesystem::path>& GetSceneRegistry() { return sceneRegistry; }
    void RegisterScene(const std::filesystem::path& path);

private: // Temporary Asset Registry
    std::unordered_map<std::string, StringID> modelNameToId;
    std::unordered_map<StringID, std::filesystem::path> modelRegistry;
    std::unordered_map<StringID, CachedModelMetadata> modelMetadataCache;

    std::unordered_map<TextureID, std::filesystem::path> textureRegistry;
    std::unordered_map<std::string, TextureID> textureNameToId;

    std::unordered_map<StringID, std::filesystem::path> cubemapRegistry;

    std::unordered_map<StringID, std::filesystem::path> sceneRegistry;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
