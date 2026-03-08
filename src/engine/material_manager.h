//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <cstdint>
#include <unordered_map>

#include "core/material_id.h"
#include "core/string_id.h"
#include "core/allocators/free_list.h"
#include "core/allocators/handle_allocator.h"
#include "core/allocators/inline_vector.h"
#include "render/render_config.h"
#include "render/shaders/model_interop.h"

namespace Core
{
struct EngineContext;
}

namespace Engine
{
using MaterialEntryHandle = Core::Handle<MaterialProperties>;

struct MaterialEntry
{
    MaterialID id{MaterialID::INVALID};
    MaterialEntryHandle handle{MaterialEntryHandle::INVALID};
    int32_t refCounter{0};

    // When ref counter is 0, if retireFrame >= renderFrame, then this will be deleted.
    uint64_t retireFrame{INT64_MAX};
};

class MaterialManager
{
public:
    explicit MaterialManager(Core::EngineContext* ctx);

    MaterialID CreateImmutableMaterial(const MaterialProperties& mat);

    void AcquireMaterial(MaterialID materialID);

    void ReleaseMaterial(MaterialID materialID);

    void ProcessRetirements();

    // const MaterialProperties& Get(MaterialID id) const
    // {
    //     return materials.at(id);
    // }
    //
    // MaterialProperties& Get(MaterialID id)
    // {
    //     return materials.at(id);
    // }
    //
    // void Update(MaterialID id, const MaterialProperties& props)
    // {
    //     materials[id] = props;
    // }

    MaterialID GetDefaultMaterial() const { return defaultMaterial; }

    const std::unordered_map<MaterialID, uint32_t>& GetIdToEntryMap() const { return idToEntryMap; }
    MaterialProperties GetImmutableProperties(MaterialID id) const
    {
        if (immutableMaterials.contains(id)) {
            return immutableMaterials.at(id);
        }

        return {};
    }
    const std::array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetActiveMaterials() const { return activeMaterialBuffer; }

    uint32_t GetMaterialIndex(MaterialID id)
    {
        if (idToEntryMap.contains(id)) {
            return idToEntryMap[id];
        }

        return UINT32_MAX;
    }

private:
    Core::EngineContext* ctx;

    std::unordered_map<MaterialID, MaterialProperties> immutableMaterials;
    MaterialID defaultMaterial{MaterialID::INVALID};

    std::array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialBuffer;
    Core::HandleAllocator<MaterialProperties, Render::BINDLESS_MATERIAL_BUFFER_COUNT> activeMaterialAllocator;
    std::unordered_map<MaterialID, uint32_t> idToEntryMap;

    // Serialized (user defined custom materials)
    std::unordered_map<StringID, MaterialProperties> mutableMaterialMap;


    static MaterialID HashMaterial(const MaterialProperties& m);
};

static MaterialProperties CreateDefaultMaterial()
{
    MaterialProperties mat{};
    mat.colorFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mat.metalRoughFactors = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

    mat.textureImageIndices = glm::ivec4(WHITE_IMAGE_BINDLESS_INDEX);
    mat.textureSamplerIndices = glm::ivec4(ASSET_SAMPLER_BINDLESS_INDEX);
    mat.textureImageIndices2 = glm::ivec4(WHITE_IMAGE_BINDLESS_INDEX);
    mat.textureSamplerIndices2 = glm::ivec4(ASSET_SAMPLER_BINDLESS_INDEX);

    mat.colorUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    mat.metalRoughUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    mat.normalUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    mat.emissiveUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    mat.occlusionUvTransform = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

    mat.emissiveFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    mat.alphaProperties = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);
    mat.physicalProperties = glm::vec4(1.5f, 0.0f, 1.0f, 1.0f);
    return mat;
}
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
