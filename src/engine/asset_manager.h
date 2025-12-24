//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_ASSET_MANAGER_H
#define WILL_ENGINE_ASSET_MANAGER_H
#include <filesystem>
#include <unordered_map>

#include "asset_manager_config.h"
#include "asset_manager_types.h"
#include "core/allocators/handle_allocator.h"
#include "render/texture_asset.h"
#include "render/model/will_model_asset.h"

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Render
{
struct ResourceManager;
struct WillModel;
}

namespace Engine
{
using ModelHandle = Core::Handle<Model>;

using InstanceHandle = Core::Handle<Instance>;

using MaterialHandle = Core::Handle<MaterialProperties>;

class AssetManager
{
public:
    explicit AssetManager(AssetLoad::AssetLoadThread* assetLoadThread, Render::ResourceManager* resourceManager);

    ~AssetManager();

    AssetManager(const AssetManager&) = delete;

    AssetManager& operator=(const AssetManager&) = delete;

    AssetManager(AssetManager&&) = delete;

    AssetManager& operator=(AssetManager&&) = delete;

    WillModelHandle LoadModel(const std::filesystem::path& path);

    Render::WillModel* GetModel(WillModelHandle handle);

    void UnloadModel(WillModelHandle handle);

    TextureHandle LoadTexture(const std::filesystem::path& path);

    Render::Texture* GetTexture(TextureHandle handle);

    void UnloadTexture(TextureHandle handle);

    void ResolveLoads(Core::FrameBuffer& stagingFrameBuffer) const;

    void ResolveUnloads();

public:
    Core::HandleAllocator<Model, Render::BINDLESS_MODEL_BUFFER_COUNT>& GetModelAllocator()
    {
        return modelEntryAllocator;
    }

    Core::HandleAllocator<Instance, Render::BINDLESS_INSTANCE_BUFFER_COUNT>& GetInstanceAllocator()
    {
        return instanceEntryAllocator;
    }

    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetMaterialAllocator()
    {
        return materialEntryAllocator;
    }

    OffsetAllocator::Allocator& GetJointMatrixAllocator()
    {
        return jointMatrixAllocator;
    }

private:
    AssetLoad::AssetLoadThread* assetLoadThread;
    Render::ResourceManager* resourceManager;

    Core::HandleAllocator<Model, Render::BINDLESS_MODEL_BUFFER_COUNT> modelEntryAllocator;
    Core::HandleAllocator<Instance, Render::BINDLESS_INSTANCE_BUFFER_COUNT> instanceEntryAllocator;
    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT> materialEntryAllocator;
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    std::unordered_map<std::filesystem::path, WillModelHandle> pathToHandle;
    Core::HandleAllocator<Render::WillModel, MAX_LOADED_MODELS> modelAllocator;
    std::array<Render::WillModel, MAX_LOADED_MODELS> models;

    Core::HandleAllocator<Render::Texture, MAX_LOADED_TEXTURES> textureAllocator;
    std::array<Render::Texture, MAX_LOADED_TEXTURES> textures{};

    std::unordered_map<std::filesystem::path, TextureHandle> pathToTextureHandle;

private: // Default Resources
    TextureHandle whiteTextureHandle;
    TextureHandle errorTextureHandle;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
