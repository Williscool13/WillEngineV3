//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H
#include <filesystem>
#include <unordered_map>

#include "asset_manager_config.h"
#include "asset_manager_types.h"
#include "material_manager.h"
#include "core/allocators/handle_allocator.h"
#include "render/types/texture_asset.h"
#include "render/types/cubemap_asset.h"
#include "render/model/model_format.h"
#include "render/model/model_types.h"
#include "render/model/will_model_asset.h"


namespace AssetLoad
{
class AsyncAssetLoadManager;
}

namespace Render
{
struct ResourceManager;
struct WillModel;
}

namespace Engine
{
struct ResolveLoadResult
{
    int32_t modelLoadedCount{0};
    int32_t textureLoadedCount{0};
    int32_t cubeLoadedCount{0};
};

class AssetManager
{
public:
    AssetManager(AssetLoad::AsyncAssetLoadManager* assetLoadManager, Render::ResourceManager* resourceManager);

    ~AssetManager();

    AssetManager(const AssetManager&) = delete;

    AssetManager& operator=(const AssetManager&) = delete;

    AssetManager(AssetManager&&) = delete;

    AssetManager& operator=(AssetManager&&) = delete;

public: // Models
    WillModelHandle LoadModel(StringID modelId);

    Render::WillModel* GetModel(WillModelHandle handle);

    void UnloadModel(WillModelHandle handle);

    const std::unordered_map<StringID, std::filesystem::path>& GetModelRegistry() { return modelRegistry; }

    struct CachedModelMetadata
    {
        Render::ModelMetadata counts;
        std::vector<Render::Node> nodes;
    };

    [[nodiscard]] const CachedModelMetadata* GetModelMetadata(StringID modelId) const;


public: // Textures
    TextureHandle LoadTexture(StringID textureId);

    Render::Texture* GetTexture(TextureHandle handle);

    void UnloadTexture(TextureHandle handle);

public: // Cubemaps
    CubemapHandle LoadCubemap(StringID cubemapId);

    Render::Cubemap* GetCubemap(CubemapHandle handle);

    void UnloadCubemap(CubemapHandle handle);

public: // Called by engine to process loads
    ResolveLoadResult ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const;

    void ResolveUnloads();

public:
    OffsetAllocator::Allocator& GetJointMatrixAllocator()
    {
        return jointMatrixAllocator;
    }

    MaterialManager& GetMaterialManager()
    {
        return materialManager;
    }

private:
    AssetLoad::AsyncAssetLoadManager* assetLoadManager;
    Render::ResourceManager* resourceManager;

    MaterialManager materialManager;

    // todo: figure out whats happening to this guy
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    std::unordered_map<StringID, WillModelHandle> modelIdToHandle;
    Core::HandleAllocator<Render::WillModel, MAX_LOADED_MODELS> modelAllocator;
    std::array<Render::WillModel, MAX_LOADED_MODELS> models;

    Core::HandleAllocator<Render::Texture, MAX_LOADED_TEXTURES> textureAllocator;
    std::array<Render::Texture, MAX_LOADED_TEXTURES> textures{};
    std::unordered_map<StringID, TextureHandle> textureIdToHandle;

    Core::HandleAllocator<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemapAllocator;
    std::array<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemaps{};
    std::unordered_map<StringID, CubemapHandle> cubemapIdToHandle;

private: // Default Resources
    TextureHandle whiteTextureHandle = TextureHandle::INVALID;
    TextureHandle errorTextureHandle = TextureHandle::INVALID;
    TextureHandle brdfLutTextureHandle = TextureHandle::INVALID;
    Render::Sampler defaultSampler;

public: // Scenes
    const std::unordered_map<StringID, std::filesystem::path>& GetSceneRegistry() { return sceneRegistry; }
    void RegisterScene(const std::filesystem::path& path);

private: // Temporary Asset Registry
    std::unordered_map<StringID, std::filesystem::path> modelRegistry;
    std::unordered_map<StringID, CachedModelMetadata> modelMetadataCache;
    std::unordered_map<StringID, std::filesystem::path> textureRegistry;
    std::unordered_map<StringID, std::filesystem::path> cubemapRegistry;
    std::unordered_map<StringID, std::filesystem::path> sceneRegistry;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
