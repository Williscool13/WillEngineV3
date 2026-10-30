//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include "resources/text_material/text_material.h"
#include "resources/text_material/text_material_format.h"

#include <chrono>

#include "asset_manager_config.h"

#include "core/containers/vector.h"
#include "core/hash/xxh3.h"
#include "engine/serialization/text_parse.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "resources/material/material_format.h"
#include "engine/include/engine_context.h"
#include "render/interface/render_interface.h"
#include "engine/logging/engine_log.h"
#include "engine/asset_manager.h"
#include "platform/file_utils.h"
#include "platform/paths.h"


namespace Engine
{
static bool SaveMaterialFile(Core::TlsfAllocator* scratch, const Core::Path& path, const WMaterialHeader& header, const Material& mat)
{
    Core::Vector<std::byte> out(scratch, Core::AllocTag::AssetManager);
    WriteWMaterialHeader(out, header);
    TextWriter w(out);
    SerializeMaterial(mat, w);
    return Platform::WriteFile(path, out.Data(), out.Size());
}

static bool SaveTextMaterialFile(Core::TlsfAllocator* scratch, const Core::Path& path, const WTextMaterialHeader& header, const TextMaterial& mat)
{
    Core::Vector<std::byte> out(scratch, Core::AllocTag::AssetManager);
    WriteWTextMaterialHeader(out, header);
    TextWriter w(out);
    SerializeTextMaterial(mat, w);
    return Platform::WriteFile(path, out.Data(), out.Size());
}

MaterialManager::MaterialManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, IAssetLoader* assetLoader)
    : ctx(ctx),
      memoryManager(&memoryManager),
      assetLoader(assetLoader),
      idToEntryMap(&memoryManager.Persistent(), Core::AllocTag::AssetManager, 2 * MAX_LOADED_MATERIALS),
      materials(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_MATERIALS),
      textMaterials(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_TEXT_MATERIALS)
{
    materialCatalog.Init(&memoryManager.Virtual(), &memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_MATERIALS, "Material");
    textMaterialCatalog.Init(&memoryManager.Virtual(), &memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_TEXT_MATERIALS, "Text material");
    uploadDirty.Init(Render::BINDLESS_MATERIAL_BUFFER_COUNT, Core::FRAME_BUFFER_COUNT, &memoryManager.Persistent(), Core::AllocTag::AssetManager);
    changedDirty.Init(Render::BINDLESS_MATERIAL_BUFFER_COUNT, 1, &memoryManager.Persistent(), Core::AllocTag::AssetManager);

    Material defaultMat{};
    defaultMat.props = {
        .colorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, // white
        .metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f}, // non-metallic, rough
        .textureImageIndices = {-1, -1, -1, -1},
        .textureSamplerIndices = {-1, -1, -1, -1},
        .textureImageIndices2 = {-1, -1, -1, -1},
        .textureSamplerIndices2 = {-1, -1, -1, -1},
        .colorUvTransform = {1.0f, 1.0f, 0.0f, 0.0f}, // identity
        .metalRoughUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .normalUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .emissiveUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .occlusionUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .emissiveFactor = {0.0f, 0.0f, 0.0f, 0.0f}, // no emission
        .alphaProperties = {0.5f, 0.0f, 0.0f, 0.0f}, // alpha cutoff, opaque, single-sided, lit
        .physicalProperties = {1.5f, 0.0f, 1.0f, 1.0f} // IOR 1.5, no dispersion, normal scale 1.0, full occlusion
    };

    // No texture refs -- factors alone define the default material appearance.
    defaultMat.samplerDesc[0] = SamplerDesc{};
    defaultMat.samplerDesc[1] = SamplerDesc{};
    defaultMat.samplerDesc[2] = SamplerDesc{};
    defaultMat.samplerDesc[3] = SamplerDesc{};
    defaultMat.samplerDesc[4] = SamplerDesc{};
    defaultMat.samplerDesc[5] = SamplerDesc{};
    defaultMaterial = CreateSynthesizedMaterial(defaultMat);

    // Default material is always resident
    AcquireMaterial(defaultMaterial);

    ctx->rescan.bMaterials.store(true, std::memory_order_release);
    Scan();
}

MaterialID MaterialManager::CreateSynthesizedMaterial(const Material& mat)
{
    MaterialID matId = HashMaterial(mat);
    if (materials.Contains(matId)) {
        return matId;
    }

    Material m = mat;
    m.name = Core::InlineString<128>("__synthesized__");
    m.id = matId;
    m.bSynthesized = true;

    materials[matId] = m;
    pendingMaterialLoadLogCount++;
    materialLoadLastActivity = std::chrono::steady_clock::now();

    return matId;
}


void MaterialManager::AcquireMaterial(MaterialID materialID)
{
    MaterialEntry* entry{nullptr};
    if (idToEntryMap.Contains(materialID)) {
        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
    }
    else {
        auto handle = activeMaterialAllocator.Add();
        idToEntryMap[materialID] = handle.index;

        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
        entry->id = materialID;
        entry->handle = handle;
    }

    bool bChanged = entry->refCounter == 0;
    entry->refCounter++;

    auto it = materials.Find(materialID);
    assert(it && "Material acquired but does not exist in the materials map");

    Material& mat = *it;

    // todo material needs to be marked as "in progress". And the model should only be loaded if the material is ready, which should only be true if all the textures and samplers are ready.
    if (!mat.bIsRuntimeLoaded) {
        auto resolveTexture = [&](TextureID id) -> int32_t {
            if (!id.IsValid()) return -1;
            Texture* tex = assetLoader->LoadTexture(id);
            return tex ? static_cast<int32_t>(tex->bindlessHandle.index) : -1;
        };

        auto resolveSampler = [&](SamplerDesc& desc) -> int32_t {
            Sampler* s = assetLoader->LoadSampler(desc);
            return s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_LINEAR_BINDLESS_INDEX;
        };

        mat.props.textureImageIndices.x = resolveTexture(mat.textureRefs[0]);
        mat.props.textureImageIndices.y = resolveTexture(mat.textureRefs[1]);
        mat.props.textureImageIndices.z = resolveTexture(mat.textureRefs[2]);
        mat.props.textureImageIndices.w = resolveTexture(mat.textureRefs[3]);
        mat.props.textureImageIndices2.x = resolveTexture(mat.textureRefs[4]);
        mat.props.textureImageIndices2.y = resolveTexture(mat.textureRefs[5]);

        mat.props.textureSamplerIndices.x = resolveSampler(mat.samplerDesc[0]);
        mat.props.textureSamplerIndices.y = resolveSampler(mat.samplerDesc[1]);
        mat.props.textureSamplerIndices.z = resolveSampler(mat.samplerDesc[2]);
        mat.props.textureSamplerIndices.w = resolveSampler(mat.samplerDesc[3]);
        mat.props.textureSamplerIndices2.x = resolveSampler(mat.samplerDesc[4]);
        mat.props.textureSamplerIndices2.y = resolveSampler(mat.samplerDesc[5]);

        mat.bIsRuntimeLoaded = true;
        pendingMaterialLoadLogCount++;
        materialLoadLastActivity = std::chrono::steady_clock::now();
        bChanged = true;
    }

    if (bChanged) { MarkDirtyIndex(entry->handle.index); }
}

void MaterialManager::ReleaseMaterial(MaterialID materialID)
{
    // Default material is always resident
    if (materialID == defaultMaterial) { return; }

    MaterialEntry* entry{nullptr};
    if (idToEntryMap.Contains(materialID)) {
        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
    }

    if (!entry) {
        LOG_WARN(Engine, "Material was released but doesn't exist in the active material buffer");
        return;
    }

    entry->refCounter--;

    if (entry->refCounter == 0) {
        entry->retireFrame = ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT + 1;
    }
}

void MaterialManager::ProcessRetirements()
{
    for (MaterialEntry& entry : activeMaterialBuffer) {
        if (!entry.handle.IsValid()) {
            continue;
        }

        if (entry.refCounter == 0) {
            if (ctx->currentRenderFrame >= entry.retireFrame) {
                auto it = materials.Find(entry.id);
                assert(it && "Material released but does not exist in the materials map");

                Material& mat = *it;
                for (TextureID texID : mat.textureRefs) {
                    if (texID.IsValid()) {
                        assetLoader->UnloadTexture(texID);
                    }
                }
                for (SamplerDesc desc : mat.samplerDesc) {
                    assetLoader->UnloadSampler(desc);
                }

                assert(mat.bIsRuntimeLoaded && "Material released but it was never runtime loaded to begin with");
                mat.bIsRuntimeLoaded = false;

                MarkDirtyIndex(entry.handle.index);
                activeMaterialAllocator.Remove(entry.handle);
                idToEntryMap.Remove(entry.id);
                entry = {};

                pendingMaterialRetireLogCount++;
                materialRetireLastActivity = std::chrono::steady_clock::now();
            }
        }
    }

    if (pendingMaterialLoadLogCount > 0 && (std::chrono::steady_clock::now() - materialLoadLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Engine, "{} material(s) loaded", pendingMaterialLoadLogCount);
        pendingMaterialLoadLogCount = 0;
    }

    if (pendingMaterialRetireLogCount > 0 && (std::chrono::steady_clock::now() - materialRetireLastActivity) >= std::chrono::seconds(ASSET_LOG_IDLE_SECONDS)) {
        LOG_INFO(Engine, "{} material(s) retired", pendingMaterialRetireLogCount);
        pendingMaterialRetireLogCount = 0;
    }
}


void MaterialManager::UpdateMutableMaterial(MaterialID id, const Material& newMat, bool bSerialize)
{
    // todo: snapshot and restore for PIE materials

    auto it = materials.Find(id);
    if (!it) { return; }
    if (it->immutable && newMat.immutable) { return; }

    Material& mat = *it;

    auto serialize = [&]() {
        WMaterialHeader header{};
        header.materialId = mat.id.id;
        const auto nameLen = std::min(mat.name.Size(), WMATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, mat.name.c_str(), nameLen);
        header.name[nameLen] = '\0';

        SaveMaterialFile(&memoryManager->AssetsScratch(), mat.sourcePath, header, mat);
    };

    // Save old resolved indices before overwriting props (caller doesn't know bindless indices)
    const glm::ivec4 oldTexIdx = mat.props.textureImageIndices;
    const glm::ivec4 oldTexIdx2 = mat.props.textureImageIndices2;
    const glm::ivec4 oldSamplerIdx = mat.props.textureSamplerIndices;
    const glm::ivec4 oldSamplerIdx2 = mat.props.textureSamplerIndices2;

    mat.props = newMat.props;
    mat.fragmentShader = newMat.fragmentShader;
    mat.lightingShader = newMat.lightingShader;

    if (!mat.bIsRuntimeLoaded) {
        for (int32_t i = 0; i < 6; ++i) {
            mat.textureRefs[i] = newMat.textureRefs[i];
            mat.samplerDesc[i] = newMat.samplerDesc[i];
        }
        if (bSerialize) { serialize(); }
        return;
    }

    // Restore old resolved indices as baseline; changed slots will be overwritten below
    mat.props.textureImageIndices = oldTexIdx;
    mat.props.textureImageIndices2 = oldTexIdx2;
    mat.props.textureSamplerIndices = oldSamplerIdx;
    mat.props.textureSamplerIndices2 = oldSamplerIdx2;

    auto texIdxRef = [&](int32_t slot) -> int32_t& {
        switch (slot) {
            case 0: return mat.props.textureImageIndices.x;
            case 1: return mat.props.textureImageIndices.y;
            case 2: return mat.props.textureImageIndices.z;
            case 3: return mat.props.textureImageIndices.w;
            case 4: return mat.props.textureImageIndices2.x;
            default: return mat.props.textureImageIndices2.y;
        }
    };
    auto samplerIdxRef = [&](int32_t slot) -> int32_t& {
        switch (slot) {
            case 0: return mat.props.textureSamplerIndices.x;
            case 1: return mat.props.textureSamplerIndices.y;
            case 2: return mat.props.textureSamplerIndices.z;
            case 3: return mat.props.textureSamplerIndices.w;
            case 4: return mat.props.textureSamplerIndices2.x;
            default: return mat.props.textureSamplerIndices2.y;
        }
    };

    for (int32_t i = 0; i < 6; ++i) {
        if (mat.textureRefs[i] != newMat.textureRefs[i]) {
            if (mat.textureRefs[i].IsValid()) {
                assetLoader->UnloadTexture(mat.textureRefs[i]);
            }
            mat.textureRefs[i] = newMat.textureRefs[i];
            Texture* tex = newMat.textureRefs[i].IsValid() ? assetLoader->LoadTexture(newMat.textureRefs[i]) : nullptr;
            texIdxRef(i) = tex ? static_cast<int32_t>(tex->bindlessHandle.index) : -1;
        }

        if (mat.samplerDesc[i] != newMat.samplerDesc[i]) {
            assetLoader->UnloadSampler(mat.samplerDesc[i]);
            mat.samplerDesc[i] = newMat.samplerDesc[i];
            Sampler* s = assetLoader->LoadSampler(mat.samplerDesc[i]);
            samplerIdxRef(i) = s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_LINEAR_BINDLESS_INDEX;
        }
    }

    MarkDirty(id);
    if (bSerialize) { serialize(); }
}

void MaterialManager::ResolveMissingTextures()
{
    int32_t resolvedCount = 0;
    for (auto pair : materials) {
        Material& mat = pair.value;
        if (!mat.bIsRuntimeLoaded) { continue; }

        auto texIdxRef = [&](int32_t slot) -> int32_t& {
            switch (slot) {
                case 0: return mat.props.textureImageIndices.x;
                case 1: return mat.props.textureImageIndices.y;
                case 2: return mat.props.textureImageIndices.z;
                case 3: return mat.props.textureImageIndices.w;
                case 4: return mat.props.textureImageIndices2.x;
                default: return mat.props.textureImageIndices2.y;
            }
        };

        for (int32_t i = 0; i < 6; ++i) {
            if (!mat.textureRefs[i].IsValid() || texIdxRef(i) != -1) { continue; }
            Texture* tex = assetLoader->LoadTexture(mat.textureRefs[i]);
            if (tex) {
                texIdxRef(i) = static_cast<int32_t>(tex->bindlessHandle.index);
                resolvedCount++;
                MarkDirty(mat.id);
            }
        }
    }
    if (resolvedCount > 0) {
        LOG_INFO(Engine, "Resolved {} previously missing material texture slot(s)", resolvedCount);
    }
}

void MaterialManager::RebindTexture(TextureID textureId, int32_t bindlessIndex)
{
    for (auto pair : materials) {
        Material& mat = pair.value;
        for (int32_t i = 0; i < 6; ++i) {
            if (mat.textureRefs[i] != textureId) { continue; }
            if (i < 4) {
                mat.props.textureImageIndices[i] = bindlessIndex;
            }
            else {
                mat.props.textureImageIndices2[i - 4] = bindlessIndex;
            }
            MarkDirty(mat.id);
        }
    }
}

void MaterialManager::MarkDirty(MaterialID id)
{
    const uint32_t* index = idToEntryMap.Find(id);
    if (index) { MarkDirtyIndex(*index); }
}

void MaterialManager::MarkDirtyIndex(uint32_t index)
{
    uploadDirty.Mark(index);
    changedDirty.Mark(index);
}

void MaterialManager::MarkAllDirty()
{
    uploadDirty.MarkRange(0, Render::BINDLESS_MATERIAL_BUFFER_COUNT);
    changedDirty.MarkRange(0, Render::BINDLESS_MATERIAL_BUFFER_COUNT);
}

void MaterialManager::SetLightingMode(Core::LightingMode mode)
{
    if (mode == uploadedLightingMode) { return; }
    uploadedLightingMode = mode;
    uploadDirty.MarkRange(0, Render::BINDLESS_MATERIAL_BUFFER_COUNT);
}

MaterialID MaterialManager::FindMutableMaterial(StringID name) const
{
    return materialCatalog.FindByName(name);
}

const Material* MaterialManager::GetMaterial(MaterialID id) const
{
    return materials.Find(id);
}

MaterialProperties MaterialManager::GetProperties(MaterialID id) const
{
    const Material* mat = materials.Find(id);
    if (mat == nullptr) {
        return MaterialProperties();
    }

    return mat->props;
}

RenderMaterial MaterialManager::GetRenderMaterial(MaterialID id) const
{
    const Material* mat = materials.Find(id);
    if (mat == nullptr) {
        return GetDefaultRenderMaterial();
    }

    return {mat->props, mat->fragmentShader, mat->lightingShader};
}

bool MaterialManager::MoveMutableMaterial(MaterialID id, std::string_view subDirectory)
{
    Material* mat = materials.Find(id);
    if (!mat || mat->bSynthesized || mat->sourcePath.IsEmpty()) { return false; }

    Core::Path newDir = Platform::GetAssetPath() / "materials";
    if (!subDirectory.empty()) {
        newDir = newDir / subDirectory;
    }
    if (newDir == mat->sourcePath.Parent()) { return false; }

    const Core::Path newPath = newDir / mat->sourcePath.Filename();
    if (Platform::FileExists(newPath)) { return false; }

    Platform::CreateDirectories(newDir.c_str());
    if (!Platform::RenameFile(mat->sourcePath, newPath)) { return false; }
    mat->sourcePath = newPath;
    if (CachedMaterialFile* file = materialCatalog.Find(id)) { file->source = newPath; }
    return true;
}

bool MaterialManager::DeleteMutableMaterial(MaterialID id)
{
    auto it = materials.Find(id);
    if (it == nullptr) { return false; }
    if (it->bSynthesized) { return false; }

    if (Platform::FileExists(it->sourcePath) && !Platform::MoveToRecycleBin(it->sourcePath)) {
        LOG_ERROR(Asset, "Failed to delete material '{}'", it->sourcePath.c_str());
        return false;
    }

    materialCatalog.Remove(id);
    materialCatalog.RebuildNames();
    materials.Remove(id);
    return true;
}

bool MaterialManager::RenameMutableMaterial(MaterialID id, std::string_view newName)
{
    Material* mat = materials.Find(id);
    if (!mat || mat->bSynthesized) { return false; }

    const StringID newSid(newName.data(), newName.size());
    if (materialCatalog.FindByName(newSid).IsValid()) { return false; }

    mat->name = Core::InlineString<128>(newName);

    if (!mat->sourcePath.IsEmpty()) {
        Core::InlineString<256> newFileName(newName);
        newFileName.Append(".wmaterial");
        Core::Path newPath = mat->sourcePath.Parent() / newFileName.c_str();

        Platform::RenameFile(mat->sourcePath, newPath);
        mat->sourcePath = newPath;

        WMaterialHeader header{};
        header.materialId = mat->id.id;
        const auto nameLen = std::min(newName.size(), WMATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, newName.data(), nameLen);
        header.name[nameLen] = '\0';

        SaveMaterialFile(&memoryManager->AssetsScratch(), newPath, header, *mat);
    }

    if (CachedMaterialFile* file = materialCatalog.Find(id)) {
        file->name = mat->name;
        file->source = mat->sourcePath;
    }
    materialCatalog.SetName(id, newSid);
    materialCatalog.RebuildNames();
    return true;
}

void MaterialManager::CreateMaterial(std::string_view name, std::string_view subDirectory)
{
    Core::Path matDir = Platform::GetAssetPath() / "materials";
    if (!subDirectory.empty()) {
        matDir = matDir / subDirectory;
    }
    Platform::CreateDirectories(matDir.c_str());
    auto fileName = Core::InlineString(name);
    fileName.Append(".wmaterial");
    const Core::Path matPath = matDir / fileName.c_str();

    Material mat{};
    mat.name = Core::InlineString<128>(name);
    mat.id = MaterialID{mutableIdRng()};
    mat.props = GetDefaultMaterialProperties();
    mat.fragmentShader = GetDefaultMaterialFragmentShader();
    mat.lightingShader = GetDefaultMaterialLightingShader();
    mat.props.colorFactor = {mutableIdRng.Float(), mutableIdRng.Float(), mutableIdRng.Float(), 1.0f}; // todo
    mat.sourcePath = matPath;

    WMaterialHeader header{};
    header.materialId = mat.id.id;
    const auto nameLen = std::min(name.size(), WMATERIAL_NAME_LENGTH - 1);
    memcpy(header.name, name.data(), nameLen);
    header.name[nameLen] = '\0';

    SaveMaterialFile(&memoryManager->AssetsScratch(), matPath, header, mat);

    ctx->rescan.bMaterials.store(true, std::memory_order_release);
}

void MaterialManager::Scan()
{
    bool expectedRescan = true;
    if (!ctx->rescan.bMaterials.compare_exchange_strong(expectedRescan, false, std::memory_order::acq_rel, std::memory_order::relaxed)) { return; }

    const uint32_t generation = ++scanGeneration;
    Core::Vector<Core::Path> paths(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Platform::RecursiveDirectoryIterator(Platform::GetAssetPath(), paths);
    for (const Core::Path& path : paths) {
        const auto ext = path.Extension();
        if (ext == ".wmaterial") { ScanMaterialFile(path, generation); }
        else if (ext == ".wtextmaterial") { ScanTextMaterialFile(path, generation); }
    }

    // In-use materials keep their entry and definition until they retire.
    materialCatalog.Prune(generation, [this](MaterialID id, const CachedMaterialFile&) { return idToEntryMap.Contains(id); });
    textMaterialCatalog.Prune(generation, [](TextMaterialID, const CachedMaterialFile&) { return false; });
    materialCatalog.RebuildNames();
    textMaterialCatalog.RebuildNames();

    Core::Vector<MaterialID> goneMaterials(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    for (const auto& [id, mat] : materials) {
        if (!mat.bSynthesized && !materialCatalog.Contains(id)) { goneMaterials.PushBack(id); }
    }
    for (const MaterialID id : goneMaterials) { materials.Remove(id); }

    Core::Vector<TextMaterialID> goneTextMaterials(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    for (const auto& [id, mat] : textMaterials) {
        if (!textMaterialCatalog.Contains(id)) { goneTextMaterials.PushBack(id); }
    }
    for (const TextMaterialID id : goneTextMaterials) { textMaterials.Remove(id); }
}

void MaterialManager::ScanMaterialFile(const Core::Path& path, uint32_t generation)
{
    Platform::ScopedFileMapping map(path);
    const auto header = map.data ? ReadWMaterialHeader(map.data, map.size) : std::nullopt;
    if (!header) {
        LOG_WARN(Asset, "Skipping {} - missing or invalid wmaterial header", path.c_str());
        return;
    }
    if (header->materialId == 0) {
        LOG_WARN(Asset, "Material '{}' has no id. Re-save it to fix", path.c_str());
        return;
    }

    const MaterialID id{header->materialId};
    const Core::InlineString<128> name{std::string_view{header->name, strnlen(header->name, WMATERIAL_NAME_LENGTH)}};
    const uint64_t contentVersion = Hash(map.data, map.size);
    const auto [file, bExisted, prevVersion] = materialCatalog.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
    file.source = path;
    file.name = name;
    file.contentVersion = contentVersion;
    if (bExisted && prevVersion == contentVersion) { return; }
    
    // TEMPORARY: permanently loads every definition at scan. Moves behind an acquire in the material revamp, leaving Scan catalog-only.
    const TextReader r(map.data + header->dataOffset, map.size - header->dataOffset);
    Material mat = DeserializeMaterial(r, path);
    mat.id = id;

    Material* existing = materials.Find(id);
    if (existing == nullptr) {
        materials[id] = mat;
        return;
    }

    // Changed on disk: re-apply through the update path so resolved textures and samplers follow.
    LOG_TRACE(Asset, "Material '{}' (id {:x}) changed on disk", name.c_str(), id.id);
    existing->name = mat.name;
    existing->sourcePath = mat.sourcePath;
    UpdateMutableMaterial(id, mat, false);
}

void MaterialManager::ScanTextMaterialFile(const Core::Path& path, uint32_t generation)
{
    Platform::ScopedFileMapping map(path);
    const auto header = map.data ? ReadWTextMaterialHeader(map.data, map.size) : std::nullopt;
    if (!header) {
        LOG_WARN(Asset, "Skipping {} - missing or invalid wtextmaterial header", path.c_str());
        return;
    }
    if (header->textMaterialId == 0) {
        LOG_WARN(Asset, "Text material '{}' has no id. Re-save it to fix", path.c_str());
        return;
    }

    const TextMaterialID id{header->textMaterialId};
    const Core::InlineString<128> name{std::string_view{header->name, strnlen(header->name, WTEXT_MATERIAL_NAME_LENGTH)}};
    const uint64_t contentVersion = Hash(map.data, map.size);
    const auto [file, bExisted, prevVersion] = textMaterialCatalog.Stamp(id, StringID{name.c_str(), name.Size()}, generation, path);
    file.source = path;
    file.name = name;
    file.contentVersion = contentVersion;
    if (bExisted && prevVersion == contentVersion) { return; }

    // TEMPORARY: permanently loads every definition at scan. Moves behind an acquire in the material revamp, leaving Scan catalog-only.
    const TextReader r(map.data + header->dataOffset, map.size - header->dataOffset);
    TextMaterial mat = DeserializeTextMaterial(r, path);
    mat.id = id;

    TextMaterial* existing = textMaterials.Find(id);
    if (existing == nullptr) {
        textMaterials[id] = mat;
        return;
    }

    LOG_TRACE(Asset, "Text material '{}' (id {:x}) changed on disk", name.c_str(), id.id);
    existing->name = mat.name;
    existing->sourcePath = mat.sourcePath;
    UpdateTextMaterial(id, mat, false);
}

void MaterialManager::CreateTextMaterial(std::string_view name)
{
    const Core::Path matDir = Platform::GetAssetPath() / "text_materials";
    Platform::CreateDirectories(matDir.c_str());
    auto fileName = Core::InlineString(name);
    fileName.Append(".wtextmaterial");
    const Core::Path matPath = matDir / fileName.c_str();

    TextMaterial mat{};
    mat.id = TextMaterialID{textMaterialIdRng()};
    mat.name = Core::InlineString<128>(name);
    mat.sourcePath = matPath;

    WTextMaterialHeader header{};
    header.textMaterialId = mat.id.id;
    const auto nameLen = std::min(name.size(), WTEXT_MATERIAL_NAME_LENGTH - 1);
    memcpy(header.name, name.data(), nameLen);
    header.name[nameLen] = '\0';

    SaveTextMaterialFile(&memoryManager->AssetsScratch(), matPath, header, mat);

    textMaterials[mat.id] = mat;
    CachedMaterialFile& file = textMaterialCatalog.Stamp(mat.id, StringID{mat.name.c_str(), mat.name.Size()}, scanGeneration, matPath).entry;
    file.source = matPath;
    file.name = mat.name;
    textMaterialCatalog.RebuildNames();

    ctx->rescan.bMaterials.store(true, std::memory_order_release);
}

void MaterialManager::UpdateTextMaterial(TextMaterialID id, const TextMaterial& newMat, bool bSerialize)
{
    TextMaterial* mat = textMaterials.Find(id);
    if (!mat) { return; }

    mat->colorTint = newMat.colorTint;
    mat->outlineColor = newMat.outlineColor;
    mat->outlineWidth = newMat.outlineWidth;
    mat->shadowSoftness = newMat.shadowSoftness;
    mat->shadowOffset = newMat.shadowOffset;
    mat->shadowColor = newMat.shadowColor;

    if (bSerialize) {
        WTextMaterialHeader header{};
        header.textMaterialId = mat->id.id;
        const auto nameLen = std::min(mat->name.Size(), WTEXT_MATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, mat->name.c_str(), nameLen);
        header.name[nameLen] = '\0';

        SaveTextMaterialFile(&memoryManager->AssetsScratch(), mat->sourcePath, header, *mat);
    }
}

bool MaterialManager::DeleteTextMaterial(TextMaterialID id)
{
    const TextMaterial* mat = textMaterials.Find(id);
    if (!mat) { return false; }

    if (Platform::FileExists(mat->sourcePath) && !Platform::MoveToRecycleBin(mat->sourcePath)) {
        LOG_ERROR(Asset, "Failed to delete text material '{}'", mat->sourcePath.c_str());
        return false;
    }

    textMaterialCatalog.Remove(id);
    textMaterialCatalog.RebuildNames();
    textMaterials.Remove(id);
    return true;
}


bool MaterialManager::RenameTextMaterial(TextMaterialID id, std::string_view newName)
{
    TextMaterial* mat = textMaterials.Find(id);
    if (!mat) { return false; }

    const StringID newSid(newName.data(), newName.size());
    if (textMaterialCatalog.FindByName(newSid).IsValid()) { return false; }

    mat->name = Core::InlineString<128>(newName);

    if (!mat->sourcePath.IsEmpty()) {
        Core::InlineString<256> newFileName(newName);
        newFileName.Append(".wtextmaterial");
        Core::Path newPath = mat->sourcePath.Parent() / newFileName.c_str();

        Platform::RenameFile(mat->sourcePath, newPath);
        mat->sourcePath = newPath;

        WTextMaterialHeader header{};
        header.textMaterialId = mat->id.id;
        const auto nameLen = std::min(newName.size(), WTEXT_MATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, newName.data(), nameLen);
        header.name[nameLen] = '\0';

        SaveTextMaterialFile(&memoryManager->AssetsScratch(), newPath, header, *mat);
    }

    if (CachedMaterialFile* file = textMaterialCatalog.Find(id)) {
        file->name = mat->name;
        file->source = mat->sourcePath;
    }
    textMaterialCatalog.SetName(id, newSid);
    textMaterialCatalog.RebuildNames();
    return true;
}

TextMaterialID MaterialManager::FindTextMaterial(StringID name) const
{
    return textMaterialCatalog.FindByName(name);
}

const TextMaterial* MaterialManager::GetTextMaterial(TextMaterialID id) const
{
    return textMaterials.Find(id);
}

TextRenderMaterial MaterialManager::GetRenderTextMaterial(TextMaterialID id) const
{
    const TextMaterial* mat = textMaterials.Find(id);
    if (!mat) {
        return TextRenderMaterial{.colorTint = {1.0f, 1.0f, 1.0f, 1.0f}};
    }
    return TextRenderMaterial{
        .colorTint = mat->colorTint,
        .outlineColor = mat->outlineColor,
        .shadowColor = mat->shadowColor,
        .shadowOffset = mat->shadowOffset,
        .outlineWidth = mat->outlineWidth,
        .shadowSoftness = mat->shadowSoftness,
    };
}
} // Engine
