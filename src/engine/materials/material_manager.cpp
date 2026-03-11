//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

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

    auto resolveTexture = [&](TextureID id) -> int32_t {
        if (!id.IsValid()) return WHITE_IMAGE_BINDLESS_INDEX;
        TextureHandle handle = assetManager->LoadTexture(id);
        if (!handle.IsValid()) return WHITE_IMAGE_BINDLESS_INDEX;
        Texture* tex = assetManager->GetTexture(handle);
        return tex ? static_cast<int32_t>(tex->bindlessHandle.index) : WHITE_IMAGE_BINDLESS_INDEX;
    };

    m.props.textureImageIndices.x  = resolveTexture(m.textureRefs[0]);
    m.props.textureImageIndices.y  = resolveTexture(m.textureRefs[1]);
    m.props.textureImageIndices.z  = resolveTexture(m.textureRefs[2]);
    m.props.textureImageIndices.w  = resolveTexture(m.textureRefs[3]);
    m.props.textureImageIndices2.x = resolveTexture(m.textureRefs[4]);
    m.props.textureImageIndices2.y = resolveTexture(m.textureRefs[5]);

    auto resolveSampler = [&](SamplerDesc& desc) -> int32_t {
        SamplerHandle handle = assetManager->LoadSampler(desc);
        if (!handle.IsValid()) return ASSET_SAMPLER_BINDLESS_INDEX;
        Sampler* s = assetManager->GetSampler(handle);
        return s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_BINDLESS_INDEX;
    };

    m.props.textureSamplerIndices.x  = resolveSampler(m.samplerDesc[0]);
    m.props.textureSamplerIndices.y  = resolveSampler(m.samplerDesc[1]);
    m.props.textureSamplerIndices.z  = resolveSampler(m.samplerDesc[2]);
    m.props.textureSamplerIndices.w  = resolveSampler(m.samplerDesc[3]);
    m.props.textureSamplerIndices2.x = resolveSampler(m.samplerDesc[4]);
    m.props.textureSamplerIndices2.y = resolveSampler(m.samplerDesc[5]);

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
    // todo: need to unref textures/samplers
    // todo: then add refs to new textures/samplers
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

void MaterialManager::SaveMutableMaterials() const
{
    for (const auto& [id, mat] : materials) {
        if (mat.immutable) { continue; }
        nlohmann::json j = SerializeMaterial(mat);
        j["version"] = WMATERIAL_VERSION;
        std::ofstream file(mat.sourcePath);
        file << j.dump(4);
    }
}

void MaterialManager::LoadMutableMaterials()
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
