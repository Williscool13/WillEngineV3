//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "material_format.h"
#include "core/include/engine_context.h"
#include "core/include/render_interface.h"
#include "engine/logging/engine_log.h"
#include "engine/asset_manager.h"
#include "platform/paths.h"


namespace Engine
{
MaterialManager::MaterialManager(Core::EngineContext* ctx, AssetManager* assetManager)
    : ctx(ctx), assetManager(assetManager)
{
    Material defaultMat{};
    defaultMat.props = {
        .colorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, // white
        .metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f}, // non-metallic, rough
        .textureImageIndices = {WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX},
        .textureSamplerIndices = {ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX},
        .textureImageIndices2 = {WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX},
        .textureSamplerIndices2 = {ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX, ASSET_SAMPLER_BINDLESS_INDEX},
        .colorUvTransform = {1.0f, 1.0f, 0.0f, 0.0f}, // identity
        .metalRoughUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .normalUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .emissiveUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .occlusionUvTransform = {1.0f, 1.0f, 0.0f, 0.0f},
        .emissiveFactor = {0.0f, 0.0f, 0.0f, 0.0f}, // no emission
        .alphaProperties = {0.5f, 0.0f, 0.0f, 0.0f}, // alpha cutoff, opaque, single-sided, lit
        .physicalProperties = {1.5f, 0.0f, 1.0f, 1.0f} // IOR 1.5, no dispersion, normal scale 1.0, full occlusion
    };

    TextureID whiteTexture = assetManager->FindTextureByName("white");
    defaultMat.textureRefs[0] = whiteTexture;
    defaultMat.textureRefs[1] = whiteTexture;
    defaultMat.textureRefs[2] = whiteTexture;
    defaultMat.textureRefs[3] = whiteTexture;
    defaultMat.textureRefs[4] = whiteTexture;
    defaultMat.textureRefs[5] = whiteTexture;
    defaultMat.samplerDesc[0] = SamplerDesc{};
    defaultMat.samplerDesc[1] = SamplerDesc{};
    defaultMat.samplerDesc[2] = SamplerDesc{};
    defaultMat.samplerDesc[3] = SamplerDesc{};
    defaultMat.samplerDesc[4] = SamplerDesc{};
    defaultMat.samplerDesc[5] = SamplerDesc{};
    defaultMaterial = CreateImmutableMaterial(defaultMat);

    // Default material is always resident
    AcquireMaterial(defaultMaterial);

    LoadMutableMaterials();
}

MaterialID MaterialManager::CreateImmutableMaterial(const Material& mat)
{
    MaterialID matId = HashMaterial(mat);
    if (materials.contains(matId)) {
        return matId;
    }

    Material m = mat;
    m.name = "__immutable__";
    m.id = matId;
    m.immutable = true;

    materials[matId] = m;
    return matId;
}


void MaterialManager::AcquireMaterial(MaterialID materialID)
{
    MaterialEntry* entry{nullptr};
    if (idToEntryMap.contains(materialID)) {
        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
    }
    else {
        auto handle = activeMaterialAllocator.Add();
        idToEntryMap[materialID] = handle.index;

        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
        entry->id = materialID;
        entry->handle = handle;
    }

    entry->refCounter++;

    auto it = materials.find(materialID);
    assert(it != materials.end() && "Material acquired but does not exist in the materials map");

    Material& mat = it->second;

    if (!mat.bIsRuntimeLoaded) {
        auto resolveTexture = [&](TextureID id) -> int32_t {
            if (!id.IsValid()) return WHITE_IMAGE_BINDLESS_INDEX;
            Texture* tex = assetManager->LoadTexture(id);
            return tex ? static_cast<int32_t>(tex->bindlessHandle.index) : WHITE_IMAGE_BINDLESS_INDEX;
        };

        auto resolveSampler = [&](SamplerDesc& desc) -> int32_t {
            Sampler* s = assetManager->LoadSampler(desc);
            return s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_BINDLESS_INDEX;
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
    }
}

void MaterialManager::ReleaseMaterial(MaterialID materialID)
{
    // Default material is always resident
    if (materialID == defaultMaterial) { return; }

    MaterialEntry* entry{nullptr};
    if (idToEntryMap.contains(materialID)) {
        entry = &activeMaterialBuffer[idToEntryMap[materialID]];
    }

    if (!entry) {
        LOG_WARN(Engine, "Material was released but doesn't exist in the active material buffer");
        return;
    }

    entry->refCounter--;

    if (entry->refCounter == 0) {
        entry->retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1;
        LOG_TRACE(Engine, "Material {} has hit ref 0, deleting in {} FIF", materials[materialID].name, Core::FRAME_BUFFER_COUNT);
    }
}

void MaterialManager::ProcessRetirements()
{
    for (size_t i = 0; i < activeMaterialBuffer.size(); i++) {
        MaterialEntry& entry = activeMaterialBuffer[i];
        if (!entry.handle.IsValid()) {
            continue;
        }

        if (entry.refCounter == 0) {
            if (ctx->currentFrame >= entry.retireFrame) {
                auto it = materials.find(entry.id);
                assert(it != materials.end() && "Material released but does not exist in the materials map");

                const Material& mat = it->second;
                for (TextureID texID : mat.textureRefs) {
                    if (texID.IsValid()) {
                        assetManager->UnloadTexture(texID);
                    }
                }
                for (SamplerDesc desc : mat.samplerDesc) {
                    assetManager->UnloadSampler(desc);
                }

                assert(it->second.bIsRuntimeLoaded && "Material released but it was never runtime loaded to begin with");
                it->second.bIsRuntimeLoaded = false;

                activeMaterialAllocator.Remove(entry.handle);
                idToEntryMap.erase(entry.id);
                entry = {};
            }
        }
    }
}


void MaterialManager::UpdateMutableMaterial(MaterialID id, const Material& newMat, bool bSerialize)
{
    // todo: snapshot and restore for PIE materials
    // todo if this is done at runtime, this needs to NOT serialize.
    auto it = materials.find(id);
    if (it == materials.end()) return;

    Material& mat = it->second;

    // Save old resolved indices before overwriting props (caller doesn't know bindless indices)
    const glm::ivec4 oldTexIdx = mat.props.textureImageIndices;
    const glm::ivec4 oldTexIdx2 = mat.props.textureImageIndices2;
    const glm::ivec4 oldSamplerIdx = mat.props.textureSamplerIndices;
    const glm::ivec4 oldSamplerIdx2 = mat.props.textureSamplerIndices2;

    mat.props = newMat.props;

    if (!mat.bIsRuntimeLoaded) {
        for (int32_t i = 0; i < 6; ++i) {
            mat.textureRefs[i] = newMat.textureRefs[i];
            mat.samplerDesc[i] = newMat.samplerDesc[i];
        }
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
                assetManager->UnloadTexture(mat.textureRefs[i]);
            }
            mat.textureRefs[i] = newMat.textureRefs[i];
            Texture* tex = newMat.textureRefs[i].IsValid() ? assetManager->LoadTexture(newMat.textureRefs[i]) : nullptr;
            texIdxRef(i) = tex ? static_cast<int32_t>(tex->bindlessHandle.index) : WHITE_IMAGE_BINDLESS_INDEX;
        }

        if (mat.samplerDesc[i] != newMat.samplerDesc[i]) {
            assetManager->UnloadSampler(mat.samplerDesc[i]);
            mat.samplerDesc[i] = newMat.samplerDesc[i];
            Sampler* s = assetManager->LoadSampler(mat.samplerDesc[i]);
            samplerIdxRef(i) = s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_BINDLESS_INDEX;
        }
    }

    if (bSerialize) {
        WMaterialHeader header{};
        header.materialId = mat.id.id;
        const auto nameLen = std::min(mat.name.size(), WMATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, mat.name.c_str(), nameLen);
        header.name[nameLen] = '\0';

        std::ofstream file(mat.sourcePath);
        WriteWMaterialHeader(file, header);
        file << SerializeMaterial(mat).dump(4);
    }
}

MaterialID MaterialManager::FindMutableMaterial(StringID name) const
{
    if (auto it = nameToMaterialMap.find(name); it != nameToMaterialMap.end()) {
        return it->second;
    }
    return MaterialID::INVALID;
}

const Material* MaterialManager::GetMaterial(MaterialID id) const
{
    if (auto it = materials.find(id); it != materials.end()) {
        return &it->second;
    }
    return nullptr;
}

MaterialProperties MaterialManager::GetProperties(MaterialID id) const
{
    if (auto it = materials.find(id); it != materials.end()) {
        return it->second.props;
    }
    return {};
}

void MaterialManager::CreateMaterial(std::string_view name)
{
    std::filesystem::path matDir = Platform::GetAssetPath() / "materials";
    std::filesystem::create_directories(matDir);
    std::filesystem::path matPath = matDir / (std::string(name) + ".wmaterial");

    Material mat{};
    mat.name = std::string(name);
    mat.id = MaterialID{mutableIdRng()};
    mat.props = GetDefaultMaterialProperties();
    std::uniform_real_distribution dist(0.0f, 1.0f);
    mat.props.colorFactor = {dist(mutableIdRng), dist(mutableIdRng), dist(mutableIdRng), 1.0f}; // todo
    mat.sourcePath = matPath;

    WMaterialHeader header{};
    header.materialId = mat.id.id;
    const auto nameLen = std::min(name.size(), WMATERIAL_NAME_LENGTH - 1);
    memcpy(header.name, name.data(), nameLen);
    header.name[nameLen] = '\0';

    std::ofstream file(matPath);
    WriteWMaterialHeader(file, header);
    file << SerializeMaterial(mat).dump(4);

    ctx->bShouldRescanMaterials.store(true, std::memory_order_release);
}

void MaterialManager::Scan()
{
    bool expectedRescan = true;
    if (ctx->bShouldRescanMaterials.compare_exchange_strong(expectedRescan, false, std::memory_order::acq_rel, std::memory_order::relaxed)) {
        std::filesystem::path assetPath = Platform::GetAssetPath();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
            if (entry.path().extension() != ".wmaterial") { continue; }

            std::ifstream file(entry.path());
            auto header = ReadWMaterialHeader(file);
            if (!header) { continue; }

            StringID sid(header->name, strnlen(header->name, WMATERIAL_NAME_LENGTH));
            if (nameToMaterialMap.contains(sid)) { continue; }

            Material mat = DeserializeMaterial(nlohmann::json::parse(file), entry.path());
            materials[mat.id] = mat;
            nameToMaterialMap[sid] = mat.id;
        }
    }
}

void MaterialManager::LoadMutableMaterials()
{
    std::filesystem::path assetPath = Platform::GetAssetPath();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
        if (entry.path().extension() != ".wmaterial") { continue; }

        std::ifstream file(entry.path());
        auto header = ReadWMaterialHeader(file);
        if (!header) {
            spdlog::warn("Skipping {} — missing or invalid wmaterial header", entry.path().string());
            continue;
        }

        Material mat = DeserializeMaterial(nlohmann::json::parse(file), entry.path());
        StringID sid(mat.name.data(), mat.name.size());
        materials[mat.id] = mat;
        nameToMaterialMap[sid] = mat.id;
    }
}
} // Engine
