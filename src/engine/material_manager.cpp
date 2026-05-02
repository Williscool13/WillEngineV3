//
// Created by William on 2025-12-30.
//

#include "material_manager.h"

#include <fstream>

#include <json/nlohmann/json.hpp>

#include "core/containers/vector.h"
#include "resources/material/material_format.h"
#include "engine/include/engine_context.h"
#include "render/interface/render_interface.h"
#include "engine/logging/engine_log.h"
#include "engine/asset_manager.h"
#include "platform/file_utils.h"
#include "platform/paths.h"


namespace Engine
{
MaterialManager::MaterialManager(Core::MemoryManager& memoryManager, Engine::EngineContext* ctx, AssetManager* assetManager)
    : ctx(ctx),
      memoryManager(&memoryManager),
      assetManager(assetManager),
      idToEntryMap(&memoryManager.Persistent(), Core::AllocTag::AssetManager, 2 * MAX_LOADED_MATERIALS),
      materials(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_MATERIALS),
      nameToMaterialMap(&memoryManager.Persistent(), Core::AllocTag::AssetManager, MAX_LOADED_MATERIALS)

{
    Material defaultMat{};
    defaultMat.props = {
        .colorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, // white
        .metalRoughFactors = {0.0f, 1.0f, 0.0f, 0.0f}, // non-metallic, rough
        .textureImageIndices = {WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX},
        .textureSamplerIndices = {ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX},
        .textureImageIndices2 = {WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX, WHITE_IMAGE_BINDLESS_INDEX},
        .textureSamplerIndices2 = {ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX, ASSET_SAMPLER_LINEAR_BINDLESS_INDEX},
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
    if (materials.Contains(matId)) {
        return matId;
    }

    Material m = mat;
    m.name = Core::InlineString<128>("__immutable__");
    m.id = matId;
    m.immutable = true;

    materials[matId] = m;
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

    entry->refCounter++;

    auto it = materials.Find(materialID);
    assert(it && "Material acquired but does not exist in the materials map");

    Material& mat = *it;

    // todo material needs to be marked as "in progress". And the model should only be loaded if the material is ready, which should only be true if all the textures and samplers are ready.
    if (!mat.bIsRuntimeLoaded) {
        auto resolveTexture = [&](TextureID id) -> int32_t {
            if (!id.IsValid()) return WHITE_IMAGE_BINDLESS_INDEX;
            Texture* tex = assetManager->LoadTexture(id);
            return tex ? static_cast<int32_t>(tex->bindlessHandle.index) : WHITE_IMAGE_BINDLESS_INDEX;
        };

        auto resolveSampler = [&](SamplerDesc& desc) -> int32_t {
            Sampler* s = assetManager->LoadSampler(desc);
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
    }
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
        entry->retireFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1;
        LOG_TRACE(Engine, "Material {} has hit ref 0, deleting in {} FIF", materials[materialID].name.c_str(), Core::FRAME_BUFFER_COUNT);
    }
}

void MaterialManager::ProcessRetirements()
{
    for (MaterialEntry& entry : activeMaterialBuffer) {
        if (!entry.handle.IsValid()) {
            continue;
        }

        if (entry.refCounter == 0) {
            if (ctx->currentFrame >= entry.retireFrame) {
                auto it = materials.Find(entry.id);
                assert(it && "Material released but does not exist in the materials map");

                Material& mat = *it;
                for (TextureID texID : mat.textureRefs) {
                    if (texID.IsValid()) {
                        assetManager->UnloadTexture(texID);
                    }
                }
                for (SamplerDesc desc : mat.samplerDesc) {
                    assetManager->UnloadSampler(desc);
                }

                assert(mat.bIsRuntimeLoaded && "Material released but it was never runtime loaded to begin with");
                mat.bIsRuntimeLoaded = false;

                activeMaterialAllocator.Remove(entry.handle);
                idToEntryMap.Remove(entry.id);
                entry = {};
            }
        }
    }
}


void MaterialManager::UpdateMutableMaterial(MaterialID id, const Material& newMat, bool bSerialize)
{
    // todo: snapshot and restore for PIE materials

    auto it = materials.Find(id);
    if (!it) { return; }

    Material& mat = *it;

    auto serialize = [&]() {
        WMaterialHeader header{};
        header.materialId = mat.id.id;
        const auto nameLen = std::min(mat.name.Size(), WMATERIAL_NAME_LENGTH - 1);
        memcpy(header.name, mat.name.c_str(), nameLen);
        header.name[nameLen] = '\0';

        std::ofstream file(mat.sourcePath.c_str());
        WriteWMaterialHeader(file, header);
        file << SerializeMaterial(mat).dump(4);
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
            samplerIdxRef(i) = s ? static_cast<int32_t>(s->bindlessHandle.index) : ASSET_SAMPLER_LINEAR_BINDLESS_INDEX;
        }
    }

    if (bSerialize) { serialize(); }
}

MaterialID MaterialManager::FindMutableMaterial(StringID name) const
{
    const MaterialID* mat = nameToMaterialMap.Find(name);;
    if (mat == nullptr) {
        return MaterialID::INVALID;
    }

    return *mat;
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

bool MaterialManager::DeleteMutableMaterial(MaterialID id)
{
    auto it = materials.Find(id);
    if (it == nullptr) { return false; }
    if (it->immutable) { return false; }

    if (!it->sourcePath.IsEmpty()) {
        Platform::DeleteSingleFile(it->sourcePath.c_str());
    }

    StringID sid(it->name.c_str(), it->name.Size());
    nameToMaterialMap.Remove(sid);
    materials.Remove(id);
    return true;
}

void MaterialManager::CreateMaterial(std::string_view name)
{
    const Core::Path matDir = Platform::GetAssetPath() / "materials";
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
    std::uniform_real_distribution dist(0.0f, 1.0f);
    mat.props.colorFactor = {dist(mutableIdRng), dist(mutableIdRng), dist(mutableIdRng), 1.0f}; // todo
    mat.sourcePath = matPath;

    WMaterialHeader header{};
    header.materialId = mat.id.id;
    const auto nameLen = std::min(name.size(), WMATERIAL_NAME_LENGTH - 1);
    memcpy(header.name, name.data(), nameLen);
    header.name[nameLen] = '\0';

    std::ofstream file(matPath.c_str());
    WriteWMaterialHeader(file, header);
    file << SerializeMaterial(mat).dump(4);

    ctx->bShouldRescanMaterials.store(true, std::memory_order_release);
}

void MaterialManager::Scan()
{
    bool expectedRescan = true;
    if (ctx->bShouldRescanMaterials.compare_exchange_strong(expectedRescan, false, std::memory_order::acq_rel, std::memory_order::relaxed)) {
        Core::Vector<Core::Path> paths(&ctx->memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
        Platform::RecursiveDirectoryIterator(Platform::GetAssetPath(), paths);

        for (uint32_t i = 0; i < paths.Size(); ++i) {
            if (paths[i].Extension() != ".wmaterial") { continue; }

            std::ifstream file(paths[i].c_str());
            auto header = ReadWMaterialHeader(file);
            if (!header) { continue; }

            StringID sid(header->name, strnlen(header->name, WMATERIAL_NAME_LENGTH));
            if (nameToMaterialMap.Contains(sid)) { continue; }

            const nlohmann::json j = nlohmann::json::parse(file);
            Material mat = DeserializeMaterial(j, paths[i]);
            materials[mat.id] = mat;
            nameToMaterialMap[sid] = mat.id;
        }
    }
}

void MaterialManager::LoadMutableMaterials()
{
    Core::Vector<Core::Path> paths(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager);
    Platform::RecursiveDirectoryIterator(Platform::GetAssetPath(), paths);

    for (uint32_t i = 0; i < paths.Size(); ++i) {
        if (paths[i].Extension() != ".wmaterial") { continue; }

        std::ifstream file(paths[i].c_str());
        auto header = ReadWMaterialHeader(file);
        if (!header) {
            LOG_WARN(Asset, "Skipping {} - missing or invalid wmaterial header", paths[i].c_str());
            continue;
        }
        const nlohmann::json j = nlohmann::json::parse(file);
        Material mat = DeserializeMaterial(j, paths[i]);
        StringID sid(mat.name.c_str(), mat.name.Size());
        materials[mat.id] = mat;
        nameToMaterialMap[sid] = mat.id;
    }
}
} // Engine
