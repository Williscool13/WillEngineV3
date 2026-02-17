//
// Created by William on 2025-12-30.
//

#ifndef WILL_ENGINE_MATERIAL_MANAGER_H
#define WILL_ENGINE_MATERIAL_MANAGER_H
#include <cstdint>
#include <unordered_map>

#include "asset-load/asset_load_config.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
using MaterialID = uint32_t;

class MaterialManager
{
public:
    MaterialManager()
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

        defaultMaterial = Create(defaultMat);
    }

    MaterialID Create(const MaterialProperties& props)
    {
        MaterialID id = nextID++;
        materials[id] = props;
        return id;
    }

    MaterialID GetOrCreate(const MaterialProperties& props)
    {
        size_t hash = HashMaterial(props);

        if (auto it = hashToID.find(hash); it != hashToID.end()) {
            return it->second; // Reuse existing
        }

        MaterialID id = nextID++;
        materials[id] = props;
        hashToID[hash] = id;
        return id;
    }

    const MaterialProperties& Get(MaterialID id) const
    {
        return materials.at(id);
    }

    MaterialProperties& Get(MaterialID id)
    {
        return materials.at(id);
    }

    void Update(MaterialID id, const MaterialProperties& props)
    {
        materials[id] = props;
    }

    MaterialID GetDefaultMaterial() const { return defaultMaterial; }

private:
    std::unordered_map<MaterialID, MaterialProperties> materials;
    std::unordered_map<size_t, MaterialID> hashToID;
    MaterialID nextID = 0;

    MaterialID defaultMaterial{};

    static size_t HashMaterial(const MaterialProperties& props)
    {
        size_t hash = 0;
        const char* data = reinterpret_cast<const char*>(&props);
        for (size_t i = 0; i < sizeof(MaterialProperties); ++i) {
            hash = hash * 31 + data[i];
        }
        return hash;
    }
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
