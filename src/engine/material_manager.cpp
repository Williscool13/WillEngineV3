//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "core/hash/fnv_1_a.h"
#include "core/include/engine_context.h"
#include "core/include/render_interface.h"
#include "logging/engine_log.h"
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

MaterialID MaterialManager::CreateImmutableMaterial(const MaterialProperties& mat)
{
    MaterialID matId = HashMaterial(mat);
    if (immutableMaterials.contains(matId)) {
        return matId;
    }
    immutableMaterials[matId] = ImmutableMaterial{mat};
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

MaterialID MaterialManager::CreateMutableMaterial(std::string_view name, const MaterialProperties& props)
{
    StringID sid(name.data(), name.size());
    if (mutableMaterialIndex.contains(sid)) {
        return mutableMaterialIndex[sid];
    }

    MaterialID id = MaterialID(mutableIdRng());
    mutableMaterials[id] = MutableMaterial{std::string(name), props};
    mutableMaterialIndex[sid] = id;
    return id;
}

void MaterialManager::UpdateMutableMaterial(MaterialID id, const MaterialProperties& props)
{
    if (auto it = mutableMaterials.find(id); it != mutableMaterials.end()) {
        it->second.props = props;
    }
}

MaterialID MaterialManager::FindMutableMaterial(StringID name) const
{
    if (auto it = mutableMaterialIndex.find(name); it != mutableMaterialIndex.end()) {
        return it->second;
    }
    return MaterialID::INVALID;
}

MaterialProperties MaterialManager::GetProperties(MaterialID id) const
{
    if (auto it = immutableMaterials.find(id); it != immutableMaterials.end()) {
        return it->second.props;
    }
    if (auto it = mutableMaterials.find(id); it != mutableMaterials.end()) {
        return it->second.props;
    }
    return {};
}

void MaterialManager::Save() const
{
    nlohmann::json root;
    root["materials"] = nlohmann::json::array();

    for (const auto& [id, mat] : mutableMaterials) {
        const MaterialProperties& p = mat.props;
        nlohmann::json entry;
        entry["name"] = mat.name;
        entry["id"] = id.id;

        entry["colorFactor"] = {p.colorFactor.x, p.colorFactor.y, p.colorFactor.z, p.colorFactor.w};
        entry["metalRoughFactors"] = {p.metalRoughFactors.x, p.metalRoughFactors.y, p.metalRoughFactors.z, p.metalRoughFactors.w};
        entry["textureImageIndices"] = {p.textureImageIndices.x, p.textureImageIndices.y, p.textureImageIndices.z, p.textureImageIndices.w};
        entry["textureSamplerIndices"] = {p.textureSamplerIndices.x, p.textureSamplerIndices.y, p.textureSamplerIndices.z, p.textureSamplerIndices.w};
        entry["textureImageIndices2"] = {p.textureImageIndices2.x, p.textureImageIndices2.y, p.textureImageIndices2.z, p.textureImageIndices2.w};
        entry["textureSamplerIndices2"] = {p.textureSamplerIndices2.x, p.textureSamplerIndices2.y, p.textureSamplerIndices2.z, p.textureSamplerIndices2.w};
        entry["colorUvTransform"] = {p.colorUvTransform.x, p.colorUvTransform.y, p.colorUvTransform.z, p.colorUvTransform.w};
        entry["metalRoughUvTransform"] = {p.metalRoughUvTransform.x, p.metalRoughUvTransform.y, p.metalRoughUvTransform.z, p.metalRoughUvTransform.w};
        entry["normalUvTransform"] = {p.normalUvTransform.x, p.normalUvTransform.y, p.normalUvTransform.z, p.normalUvTransform.w};
        entry["emissiveUvTransform"] = {p.emissiveUvTransform.x, p.emissiveUvTransform.y, p.emissiveUvTransform.z, p.emissiveUvTransform.w};
        entry["occlusionUvTransform"] = {p.occlusionUvTransform.x, p.occlusionUvTransform.y, p.occlusionUvTransform.z, p.occlusionUvTransform.w};
        entry["emissiveFactor"] = {p.emissiveFactor.x, p.emissiveFactor.y, p.emissiveFactor.z, p.emissiveFactor.w};
        entry["alphaProperties"] = {p.alphaProperties.x, p.alphaProperties.y, p.alphaProperties.z, p.alphaProperties.w};
        entry["physicalProperties"] = {p.physicalProperties.x, p.physicalProperties.y, p.physicalProperties.z, p.physicalProperties.w};

        root["materials"].push_back(entry);
    }

    std::filesystem::path path = Platform::GetAssetPath() / "material_registry.wengine";
    std::ofstream file(path);
    file << root.dump(4);
}

void MaterialManager::Load()
{
    std::filesystem::path path = Platform::GetAssetPath() / "material_registry.wengine";
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream file(path);
    nlohmann::json root = nlohmann::json::parse(file);

    for (const auto& entry : root["materials"]) {
        std::string name = entry["name"].get<std::string>();
        MaterialID id = MaterialID(entry["id"].get<uint64_t>());

        auto readF4 = [&](const char* key, glm::vec4& v) {
            const auto& a = entry[key];
            v = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
        };
        auto readI4 = [&](const char* key, glm::ivec4& v) {
            const auto& a = entry[key];
            v = {a[0].get<int32_t>(), a[1].get<int32_t>(), a[2].get<int32_t>(), a[3].get<int32_t>()};
        };

        MaterialProperties p{};
        readF4("colorFactor", p.colorFactor);
        readF4("metalRoughFactors", p.metalRoughFactors);
        readI4("textureImageIndices", p.textureImageIndices);
        readI4("textureSamplerIndices", p.textureSamplerIndices);
        readI4("textureImageIndices2", p.textureImageIndices2);
        readI4("textureSamplerIndices2", p.textureSamplerIndices2);
        readF4("colorUvTransform", p.colorUvTransform);
        readF4("metalRoughUvTransform", p.metalRoughUvTransform);
        readF4("normalUvTransform", p.normalUvTransform);
        readF4("emissiveUvTransform", p.emissiveUvTransform);
        readF4("occlusionUvTransform", p.occlusionUvTransform);
        readF4("emissiveFactor", p.emissiveFactor);
        readF4("alphaProperties", p.alphaProperties);
        readF4("physicalProperties", p.physicalProperties);

        StringID sid(name.c_str(), name.size());
        mutableMaterials[id] = MutableMaterial{name, p};
        mutableMaterialIndex[sid] = id;
    }
}

MaterialID MaterialManager::HashMaterial(const MaterialProperties& m)
{
    return MaterialID(fnv1a64(reinterpret_cast<const uint8_t*>(&m), sizeof(MaterialProperties)));
}
} // Engine
