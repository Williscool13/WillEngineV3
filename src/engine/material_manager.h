//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <cstdint>
#include <random>

#include "resources/material/material.h"
#include "core/string_id.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "engine/core/material_id.h"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
struct EngineContext;
class AssetManager;

class MaterialManager
{
public:
    MaterialManager(Core::MemoryManager& memoryManager, EngineContext* ctx, AssetManager* assetManager);

    MaterialID CreateImmutableMaterial(const Material& mat);

    /**
     * Update a mutable material's contents. Textures and Samplers will resolve changes with the asset manager.
     * \n
     * @param id
     * @param material
     * @param bSerialize Should be false if called at runtime
     */
    void UpdateMutableMaterial(MaterialID id, const Material& material, bool bSerialize = false);

    [[nodiscard]] MaterialID FindMutableMaterial(StringID name) const;

    void AcquireMaterial(MaterialID materialID);

    void ReleaseMaterial(MaterialID materialID);

    void ProcessRetirements();

    void CreateMaterial(std::string_view name);

    bool DeleteMutableMaterial(MaterialID id);

    void Scan();

    void LoadMutableMaterials();

    [[nodiscard]] MaterialID GetDefaultMaterial() const { return defaultMaterial; }
    [[nodiscard]] bool DoesMutableMaterialExist(MaterialID materialID) const { return materials.Contains(materialID); }
    [[nodiscard]] const MaterialProperties& GetDefaultMaterialProperties() const { return materials.At(defaultMaterial).props; }
    [[nodiscard]] const Core::FixedMap<MaterialID, uint32_t>& GetIdToEntryMap() const { return idToEntryMap; }

    [[nodiscard]] MaterialProperties GetProperties(MaterialID id) const;

    [[nodiscard]] const Material* GetMaterial(MaterialID id) const;

    [[nodiscard]] const uint32_t GetActiveMaterialCount() const { return activeMaterialAllocator.GetCount(); }
    [[nodiscard]] const Core::FixedMap<MaterialID, Material>& GetMaterials() const { return materials; }
    [[nodiscard]] const Core::Array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetActiveMaterials() const { return activeMaterialBuffer; }

    uint32_t GetMaterialIndex(MaterialID id)
    {
        if (idToEntryMap.Contains(id)) {
            return idToEntryMap[id];
        }

        return UINT32_MAX;
    }

private:
    Engine::EngineContext* ctx;
    Core::MemoryManager* memoryManager;
    AssetManager* assetManager;

    MaterialID defaultMaterial{MaterialID::INVALID};

    Core::Array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialBuffer;
    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialAllocator;

    Core::FixedMap<MaterialID, uint32_t> idToEntryMap;

    /**
     * Contains:
     *  - Runtime generated materials from models. Immutable so we can alias by hashing.
     *  - User defined materials referenced by String ID (see `nameToMaterialMap`). Designed to be modifiable at runtime.
     */
    Core::FixedMap<MaterialID, Material> materials;
    Core::FixedMap<StringID, MaterialID> nameToMaterialMap;

    std::mt19937_64 mutableIdRng{std::random_device{}()};
};
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
