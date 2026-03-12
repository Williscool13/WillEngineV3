//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <cstdint>
#include <random>
#include <unordered_map>

#include "material.h"
#include "core/string_id.h"
#include "core/allocators/free_list.h"
#include "core/allocators/handle_allocator.h"
#include "engine/core/material_id.h"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"

namespace Core
{
struct EngineContext;
}

namespace Engine
{
class AssetManager;

class MaterialManager
{
public:
    MaterialManager(Core::EngineContext* ctx, AssetManager* assetManager);

    MaterialID CreateImmutableMaterial(const Material& mat);

    MaterialID CreateMutableMaterial(const std::filesystem::path& src, std::string_view name, StringID materialID, const MaterialProperties& props);

    void UpdateMutableMaterial(MaterialID id, const MaterialProperties& props);

    MaterialID FindMutableMaterial(StringID name) const;

    void AcquireMaterial(MaterialID materialID);

    void ReleaseMaterial(MaterialID materialID);

    void ProcessRetirements();

    void SaveMutableMaterials() const;

    void LoadMutableMaterials();

    [[nodiscard]] MaterialID GetDefaultMaterial() const { return defaultMaterial; }
    [[nodiscard]] const MaterialProperties& GetDefaultMaterialProperties() const { return materials.at(defaultMaterial).props; }
    [[nodiscard]] const std::unordered_map<MaterialID, uint32_t>& GetIdToEntryMap() const { return idToEntryMap; }

    [[nodiscard]] MaterialProperties GetProperties(MaterialID id) const;

    [[nodiscard]] const Material* GetMaterial(MaterialID id) const;

    [[nodiscard]] const std::array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetActiveMaterials() const { return activeMaterialBuffer; }

    uint32_t GetMaterialIndex(MaterialID id)
    {
        if (idToEntryMap.contains(id)) {
            return idToEntryMap[id];
        }

        return UINT32_MAX;
    }

private:
    Core::EngineContext* ctx;
    AssetManager* assetManager;

    MaterialID defaultMaterial{MaterialID::INVALID};

    std::array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialBuffer;
    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialAllocator;
    std::unordered_map<MaterialID, uint32_t> idToEntryMap;


    /**
     * Contains:
     *  - Runtime generated materials from models. Immutable so we can alias by hashing.
     *  - User defined materials referenced by String ID (see `nameToMaterialMap`). Designed to be modifiable at runtime.
     */
    std::unordered_map<MaterialID, Material> materials;
    std::unordered_map<StringID, MaterialID> nameToMaterialMap;

    std::mt19937_64 mutableIdRng{std::random_device{}()};
};
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
