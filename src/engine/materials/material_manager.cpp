//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "core/hash/fnv_1_a.h"
#include "core/include/engine_context.h"
#include "core/include/render_interface.h"
#include "../logging/engine_log.h"
#include "platform/paths.h"


namespace Engine
{
MaterialManager::MaterialManager(Core::EngineContext* ctx)
    : ctx(ctx)
{
    MaterialProperties defaultMat{
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

    defaultMaterial = CreateImmutableMaterial(defaultMat);

    // Default material is always resident
    AcquireMaterial(defaultMaterial);

    Load();
}

MaterialID MaterialManager::CreateImmutableMaterial(const MaterialProperties& props)
{
    MaterialID matId = HashMaterial(props);
    if (materials.contains(matId)) {
        return matId;
    }

    Material m{};
    m.name = "__immutable__";
    m.id = matId;
    m.props = props;
    m.immutable = true;

    materials[matId] = m;
    return matId;
}

MaterialID MaterialManager::CreateMutableMaterial(const std::filesystem::path& src, std::string_view name, StringID pipelineID, const MaterialProperties& props)
{
    auto id = MaterialID(mutableIdRng());
    Material m{};
    m.name = name;
    m.id = id;
    m.props = props;
    m.sourcePath = src;
    m.pipelineID = pipelineID;
    m.immutable = false;

    StringID sid(name.data(), name.size());
    materials[id] = m;
    nameToMaterialMap[sid] = id;
    return id;
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
}

void MaterialManager::ReleaseMaterial(MaterialID materialID)
{
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
        entry->retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT;
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
                activeMaterialAllocator.Remove(entry.handle);
                idToEntryMap.erase(entry.id);
                entry = {};
            }
        }
    }
}


void MaterialManager::UpdateMutableMaterial(MaterialID id, const MaterialProperties& props)
{
    if (auto it = materials.find(id); it != materials.end()) {
        it->second.props = props;
    }
}

MaterialID MaterialManager::FindMutableMaterial(StringID name) const
{
    if (auto it = nameToMaterialMap.find(name); it != nameToMaterialMap.end()) {
        return it->second;
    }
    return MaterialID::INVALID;
}

MaterialProperties MaterialManager::GetProperties(MaterialID id) const
{
    if (auto it = materials.find(id); it != materials.end()) {
        return it->second.props;
    }
    if (auto it = materials.find(id); it != materials.end()) {
        return it->second.props;
    }
    return {};
}

void MaterialManager::Save() const
{
    for (const auto& [id, mat] : materials) {
        if (mat.immutable) { continue; }
        nlohmann::json j = SerializeMaterial(mat);
        j["version"] = WMATERIAL_VERSION;
        std::ofstream file(mat.sourcePath);
        file << j.dump(4);
    }
}

void MaterialManager::Load()
{
    std::filesystem::path assetPath = Platform::GetAssetPath();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assetPath)) {
        if (entry.path().extension() != ".wmaterial") { continue; }

        std::ifstream file(entry.path());
        nlohmann::json j = nlohmann::json::parse(file);

        int32_t version = j.value("version", 0);
        if (version != WMATERIAL_VERSION) {
            spdlog::warn("Skipping {} — version mismatch (got {}, expected {})", entry.path().string(), version, WMATERIAL_VERSION);
            continue;
        }

        Material mat = DeserializeMaterial(j, entry.path());
        StringID sid(mat.name.data(), mat.name.size());
        materials[mat.id] = mat;
        nameToMaterialMap[sid] = mat.id;
    }
}
} // Engine
