//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <chrono>
#include <cstdint>
#include <random>

#include "resources/material/material.h"
#include "resources/text_material/text_material.h"
#include "core/string_id.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "engine/core/material_id.h"
#include "engine/core/text_material_id.h"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"
#include "render/shaders/text_interop.h"

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

    bool RenameMutableMaterial(MaterialID id, std::string_view newName);

    void Scan();

    /** Retries texture resolve for runtime-loaded materials with unresolved slots; call after a rescan makes new textures loadable (e.g. freshly generated stubs). */
    void ResolveMissingTextures();

    void LoadMutableMaterials();

    // TextMaterial management
    void CreateTextMaterial(std::string_view name);
    void UpdateTextMaterial(TextMaterialID id, const TextMaterial& mat, bool bSerialize = true);
    bool DeleteTextMaterial(TextMaterialID id);

    bool RenameTextMaterial(TextMaterialID id, std::string_view newName);
    [[nodiscard]] TextMaterialID FindTextMaterial(StringID name) const;
    [[nodiscard]] const TextMaterial* GetTextMaterial(TextMaterialID id) const;
    [[nodiscard]] TextRenderMaterial GetRenderTextMaterial(TextMaterialID id) const;
    [[nodiscard]] const Core::FixedMap<TextMaterialID, TextMaterial>& GetTextMaterials() const { return textMaterials; }

    [[nodiscard]] MaterialID GetDefaultMaterialID() const { return defaultMaterial; }
    [[nodiscard]] const MaterialProperties& GetDefaultMaterialProperties() const { return materials.At(defaultMaterial).props; }
    [[nodiscard]] const StringID& GetDefaultMaterialFragmentShader() const { return materials.At(defaultMaterial).fragmentShader; }
    [[nodiscard]] const StringID& GetDefaultMaterialLightingShader() const { return materials.At(defaultMaterial).lightingShader; }

    [[nodiscard]] RenderMaterial GetDefaultRenderMaterial() const
    {
        Material mat = materials.At(defaultMaterial);
        return {mat.props, mat.fragmentShader, mat.lightingShader};
    }

    [[nodiscard]] bool DoesMutableMaterialExist(MaterialID materialID) const { return materials.Contains(materialID); }
    [[nodiscard]] const Core::FixedMap<MaterialID, uint32_t>& GetIdToEntryMap() const { return idToEntryMap; }


    [[nodiscard]] MaterialProperties GetProperties(MaterialID id) const;

    [[nodiscard]] RenderMaterial GetRenderMaterial(MaterialID id) const;

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
    EngineContext* ctx;
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

    int32_t pendingMaterialLoadLogCount{0};
    std::chrono::steady_clock::time_point materialLoadLastActivity{};

    int32_t pendingMaterialRetireLogCount{0};
    std::chrono::steady_clock::time_point materialRetireLastActivity{};

    std::mt19937_64 mutableIdRng{std::random_device{}()};

    Core::FixedMap<TextMaterialID, TextMaterial> textMaterials;
    Core::FixedMap<StringID, TextMaterialID> nameToTextMaterialMap;

    std::mt19937_64 textMaterialIdRng{std::random_device{}()};
};
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
