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
#include "render/vulkan/vk_resource_manager.h"

namespace AssetLoad
{
class AssetLoadThread;
}

namespace Render
{
struct WillModel;
}

namespace Engine
{
struct ModelEntry
{};

using ModelEntryHandle = Core::Handle<ModelEntry>;

struct InstanceEntry
{};

using InstanceEntryHandle = Core::Handle<InstanceEntry>;

struct MaterialEntry
{};

using MaterialEntryHandle = Core::Handle<MaterialEntry>;

class AssetManager
{

public:
    explicit AssetManager(AssetLoad::AssetLoadThread* assetLoadThread);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    AssetManager(AssetManager&&) = delete;
    AssetManager& operator=(AssetManager&&) = delete;

    WillModelHandle LoadModel(const std::filesystem::path& path);
    Render::WillModel* GetModel(WillModelHandle handle);
    void UnloadModel(WillModelHandle handle);

    void ResolveModelLoad(Core::FrameBuffer& stagingFrameBuffer);
    void ResolveModelUnload();

public:
    Core::HandleAllocator<ModelEntry, Render::BINDLESS_MODEL_BUFFER_COUNT>& GetModelAllocator()
    {
        return modelEntryAllocator;
    }

    Core::HandleAllocator<InstanceEntry, Render::BINDLESS_INSTANCE_BUFFER_COUNT>& GetInstanceAllocator()
    {
        return instanceEntryAllocator;
    }

    Core::HandleAllocator<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetMaterialAllocator()
    {
        return materialEntryAllocator;
    }

    OffsetAllocator::Allocator& GetJointMatrixAllocator()
    {
        return jointMatrixAllocator;
    }

private:
    AssetLoad::AssetLoadThread* assetLoadThread;

    Core::HandleAllocator<ModelEntry, Render::BINDLESS_MODEL_BUFFER_COUNT> modelEntryAllocator;
    Core::HandleAllocator<InstanceEntry, Render::BINDLESS_INSTANCE_BUFFER_COUNT> instanceEntryAllocator;
    Core::HandleAllocator<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT> materialEntryAllocator;
    // OffsetAllocator because it's always contiguous
    OffsetAllocator::Allocator jointMatrixAllocator{Render::BINDLESS_MODEL_BUFFER_SIZE};

    std::unordered_map<std::filesystem::path, WillModelHandle> pathToHandle;
    Core::HandleAllocator<Render::WillModel, MAX_LOADED_MODELS> modelAllocator;
    std::array<Render::WillModel, MAX_LOADED_MODELS> models;
};
} // Engine

#endif //WILL_ENGINE_ASSET_MANAGER_H
