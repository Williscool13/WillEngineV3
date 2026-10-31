//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <chrono>
#include <cstdint>

#include "resources/material/material.h"
#include "resources/text_material/text_material.h"
#include "core/math/random.h"
#include "core/string_id.h"
#include "core/containers/array.h"
#include "core/containers/fixed_map.h"
#include "core/memory/dirty_bits.h"
#include "core/memory/handle_allocator.h"
#include "core/memory/memory_manager.h"
#include "engine/core/material_id.h"
#include "engine/core/text_material_id.h"
#include "engine/catalog_table.h"
#include "engine/material_store.h"
#include "render/render_config.h"
#include "render/interface/render_params.h"
#include "render/shaders/model_interop.h"
#include "render/shaders/text_interop.h"

namespace Engine
{
struct EngineContext;
class IAssetLoader;

/** Active material slots whose GPU payload changed, one set per host buffer slot. A service of its own so draining it does not conflict with MaterialManager readers. */
class MaterialUploadDirty
{
public:
    static constexpr const char* COMPONENT_NAME = "MaterialUploadDirty";

    void Init(Core::TlsfAllocator* alloc, Core::AllocTag tag);

    void Mark(uint32_t slot) { bits.Mark(slot); }

    void SetLightingMode(Core::LightingMode mode)
    {
        if (mode == uploadedLightingMode) { return; }
        uploadedLightingMode = mode;
        bits.MarkRange(0, Render::BINDLESS_MATERIAL_BUFFER_COUNT);
    }

    /** Fn: emit(offset, count) */
    template<typename Fn>
    void Drain(uint32_t setIndex, uint32_t limit, Fn&& emit) { bits.Drain(setIndex, limit, std::forward<Fn>(emit)); }

private:
    Core::DirtyBits bits;
    Core::LightingMode uploadedLightingMode{Core::LightingMode::Analytic};
};

/** Active material slots whose payload changed since the last drain, independent of upload. */
class MaterialChangedDirty
{
public:
    static constexpr const char* COMPONENT_NAME = "MaterialChangedDirty";

    void Init(Core::TlsfAllocator* alloc, Core::AllocTag tag) { bits.Init(Render::BINDLESS_MATERIAL_BUFFER_COUNT, 1, alloc, tag); }

    void Mark(uint32_t slot) { bits.Mark(slot); }

    /** Fn: emit(offset, count) */
    template<typename Fn>
    void Drain(uint32_t limit, Fn&& emit) { bits.Drain(0, limit, std::forward<Fn>(emit)); }

private:
    Core::DirtyBits bits;
};

class MaterialManager final : public IMaterialStore
{
public:
    static constexpr const char* COMPONENT_NAME = "MaterialManager";

    /** Catalog record of a .wmaterial or .wtextmaterial; the definition itself lives in GetMaterials / GetTextMaterials. */
    struct CachedMaterialFile
    {
        Core::Path source;
        Core::InlineString<128> name;
        /** Hash of the file; the format has no content version. */
        uint64_t contentVersion{0};
    };

    MaterialManager(Core::MemoryManager& memoryManager, EngineContext* ctx, IAssetLoader* assetLoader);

    MaterialID CreateSynthesizedMaterial(const Material& mat);

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

    /**
     * @param name
     * @param subDirectory Folder under assets/materials to create the .wmaterial in. Empty means the materials root.
     */
    void CreateMaterial(std::string_view name, std::string_view subDirectory = {});

    /**
     * Moves the material's .wmaterial file on disk.
     * @param id
     * @param subDirectory Under assets/materials. Empty means the materials root.
     */
    bool MoveMutableMaterial(MaterialID id, std::string_view subDirectory);

    bool DeleteMutableMaterial(MaterialID id);

    bool RenameMutableMaterial(MaterialID id, std::string_view newName);

    void Scan();

    /** Retries texture resolve for runtime-loaded materials with unresolved slots; call after a rescan makes new textures loadable (e.g. freshly generated stubs). */
    void ResolveMissingTextures();

    void RebindTexture(TextureID textureId, int32_t bindlessIndex);

    // TextMaterial management
    void CreateTextMaterial(std::string_view name);
    void UpdateTextMaterial(TextMaterialID id, const TextMaterial& mat, bool bSerialize = true);
    bool DeleteTextMaterial(TextMaterialID id);

    bool RenameTextMaterial(TextMaterialID id, std::string_view newName);
    [[nodiscard]] TextMaterialID FindTextMaterial(StringID name) const;
    [[nodiscard]] const TextMaterial* GetTextMaterial(TextMaterialID id) const;
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

    [[nodiscard]] const uint32_t GetActiveMaterialCount() const { return activeMaterialAllocator.GetCount(); }
    [[nodiscard]] const Core::FixedMap<MaterialID, Material>& GetMaterials() const { return materials; }

public: // IMaterialStore
    [[nodiscard]] const Core::Array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetActiveMaterials() const override { return activeMaterialBuffer; }

    [[nodiscard]] RenderMaterial GetRenderMaterial(MaterialID id) const override;

    [[nodiscard]] const Material* GetMaterial(MaterialID id) const override;

    [[nodiscard]] TextRenderMaterial GetRenderTextMaterial(TextMaterialID id) const override;

public:
    uint32_t GetMaterialIndex(MaterialID id)
    {
        if (idToEntryMap.Contains(id)) {
            return idToEntryMap[id];
        }

        return UINT32_MAX;
    }

    [[nodiscard]] MaterialUploadDirty* GetUploadDirty() { return &uploadDirty; }
    [[nodiscard]] MaterialChangedDirty* GetChangedDirty() { return &changedDirty; }

private:
    void MarkDirty(MaterialID id);

    void MarkDirtyIndex(uint32_t index);

    void ScanMaterialFile(const Core::Path& path, uint32_t generation);

    void ScanTextMaterialFile(const Core::Path& path, uint32_t generation);

private:
    EngineContext* ctx;
    Core::MemoryManager* memoryManager;
    IAssetLoader* assetLoader;

    MaterialID defaultMaterial{MaterialID::INVALID};

    Core::Array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialBuffer;
    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialAllocator;

    Core::FixedMap<MaterialID, uint32_t> idToEntryMap;
    MaterialUploadDirty uploadDirty;
    MaterialChangedDirty changedDirty;

    /**
     * Contains:
     *  - Runtime generated materials from models. Immutable so we can alias by hashing.
     *  - User defined materials from .wmaterial files (see `materialCatalog`). Designed to be modifiable at runtime.
     */
    Core::FixedMap<MaterialID, Material> materials;
    CatalogTable<MaterialID, CachedMaterialFile> materialCatalog;

    int32_t pendingMaterialLoadLogCount{0};
    std::chrono::steady_clock::time_point materialLoadLastActivity{};

    int32_t pendingMaterialRetireLogCount{0};
    std::chrono::steady_clock::time_point materialRetireLastActivity{};

    Core::Random mutableIdRng{};

    Core::FixedMap<TextMaterialID, TextMaterial> textMaterials;
    CatalogTable<TextMaterialID, CachedMaterialFile> textMaterialCatalog;
    uint32_t scanGeneration{0};

    Core::Random textMaterialIdRng{};
};
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
