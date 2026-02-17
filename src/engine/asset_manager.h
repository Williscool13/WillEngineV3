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
#include "render/model/will_model_asset.h"
#include "audio/audio_asset.h"


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
    WillModelHandle LoadModel(const std::filesystem::path& path);

    Render::WillModel* GetModel(WillModelHandle handle);

    void UnloadModel(WillModelHandle handle);

public: // Textures
    TextureHandle LoadTexture(const std::filesystem::path& path);

    Render::Texture* GetTexture(TextureHandle handle);

    void UnloadTexture(TextureHandle handle);

public: // Cubemaps
    CubemapHandle LoadCubemap(const std::filesystem::path& path);

    Render::Cubemap* GetCubemap(CubemapHandle handle);

    void UnloadCubemap(CubemapHandle handle);

public: // Called by engine to process loads
    void ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const;

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

    std::unordered_map<std::filesystem::path, WillModelHandle> pathToHandle;
    Core::HandleAllocator<Render::WillModel, MAX_LOADED_MODELS> modelAllocator;
    std::array<Render::WillModel, MAX_LOADED_MODELS> models;

    Core::HandleAllocator<Render::Texture, MAX_LOADED_TEXTURES> textureAllocator;
    std::array<Render::Texture, MAX_LOADED_TEXTURES> textures{};
    std::unordered_map<std::filesystem::path, TextureHandle> pathToTextureHandle;

    Core::HandleAllocator<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemapAllocator;
    std::array<Render::Cubemap, MAX_LOADED_CUBEMAPS> cubemaps{};
    std::unordered_map<std::filesystem::path, CubemapHandle> pathToCubemapHandle;

private: // Default Resources
    TextureHandle whiteTextureHandle = TextureHandle::INVALID;
    TextureHandle errorTextureHandle = TextureHandle::INVALID;
    TextureHandle brdfLutTextureHandle = TextureHandle::INVALID;
    Render::Sampler defaultSampler;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
