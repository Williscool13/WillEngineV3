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
            .textureImageIndices = {AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX},
            .textureSamplerIndices = {AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX},
            .textureImageIndices2 = {AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX, AssetLoad::WHITE_IMAGE_BINDLESS_INDEX},
            .textureSamplerIndices2 = {AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX, AssetLoad::DEFAULT_SAMPLER_BINDLESS_INDEX},
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
}

#endif //WILL_ENGINE_MATERIAL_MANAGER_H
