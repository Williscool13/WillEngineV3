//
// Created by William on 2026-03-09.
//

#include "material.h"

#include "engine/serialization/serialization.h"

namespace Engine
{
MaterialID HashMaterial(const Material& m)
{
    struct StableKey
    {
        glm::vec4 colorFactor;
        glm::vec4 metalRoughFactors;
        glm::vec4 colorUvTransform;
        glm::vec4 metalRoughUvTransform;
        glm::vec4 normalUvTransform;
        glm::vec4 emissiveUvTransform;
        glm::vec4 occlusionUvTransform;
        glm::vec4 emissiveFactor;
        glm::vec4 alphaProperties;
        glm::vec4 physicalProperties;
        TextureID textureRefs[6];
        SamplerDesc samplerDesc[6];
    };

    StableKey key{};
    key.colorFactor = m.props.colorFactor;
    key.metalRoughFactors = m.props.metalRoughFactors;
    key.colorUvTransform = m.props.colorUvTransform;
    key.metalRoughUvTransform = m.props.metalRoughUvTransform;
    key.normalUvTransform = m.props.normalUvTransform;
    key.emissiveUvTransform = m.props.emissiveUvTransform;
    key.occlusionUvTransform = m.props.occlusionUvTransform;
    key.emissiveFactor = m.props.emissiveFactor;
    key.alphaProperties = m.props.alphaProperties;
    key.physicalProperties = m.props.physicalProperties;
    for (int i = 0; i < 6; ++i) key.textureRefs[i] = m.textureRefs[i];
    for (int i = 0; i < 6; ++i) key.samplerDesc[i] = m.samplerDesc[i];

    return MaterialID(fnv1a64(reinterpret_cast<const uint8_t*>(&key), sizeof(StableKey)));
}

nlohmann::json SerializeMaterial(const Material& mat)
{
    assert(!mat.immutable);
    nlohmann::json j;

    j["name"] = mat.name;
    j["id"] = mat.id.id;
    j["pipeline"] = mat.pipelineID.id;
    j["pipelineName"] = mat.pipelineID.ToString();

    j["colorFactor"] = {mat.props.colorFactor.x, mat.props.colorFactor.y, mat.props.colorFactor.z, mat.props.colorFactor.w};
    j["metalRoughFactors"] = {mat.props.metalRoughFactors.x, mat.props.metalRoughFactors.y, mat.props.metalRoughFactors.z, mat.props.metalRoughFactors.w};
    j["textureImageIndices"] = {mat.props.textureImageIndices.x, mat.props.textureImageIndices.y, mat.props.textureImageIndices.z, mat.props.textureImageIndices.w};
    j["textureSamplerIndices"] = {mat.props.textureSamplerIndices.x, mat.props.textureSamplerIndices.y, mat.props.textureSamplerIndices.z, mat.props.textureSamplerIndices.w};
    j["textureImageIndices2"] = {mat.props.textureImageIndices2.x, mat.props.textureImageIndices2.y, mat.props.textureImageIndices2.z, mat.props.textureImageIndices2.w};
    j["textureSamplerIndices2"] = {mat.props.textureSamplerIndices2.x, mat.props.textureSamplerIndices2.y, mat.props.textureSamplerIndices2.z, mat.props.textureSamplerIndices2.w};
    j["colorUvTransform"] = {mat.props.colorUvTransform.x, mat.props.colorUvTransform.y, mat.props.colorUvTransform.z, mat.props.colorUvTransform.w};
    j["metalRoughUvTransform"] = {mat.props.metalRoughUvTransform.x, mat.props.metalRoughUvTransform.y, mat.props.metalRoughUvTransform.z, mat.props.metalRoughUvTransform.w};
    j["normalUvTransform"] = {mat.props.normalUvTransform.x, mat.props.normalUvTransform.y, mat.props.normalUvTransform.z, mat.props.normalUvTransform.w};
    j["emissiveUvTransform"] = {mat.props.emissiveUvTransform.x, mat.props.emissiveUvTransform.y, mat.props.emissiveUvTransform.z, mat.props.emissiveUvTransform.w};
    j["occlusionUvTransform"] = {mat.props.occlusionUvTransform.x, mat.props.occlusionUvTransform.y, mat.props.occlusionUvTransform.z, mat.props.occlusionUvTransform.w};
    j["emissiveFactor"] = {mat.props.emissiveFactor.x, mat.props.emissiveFactor.y, mat.props.emissiveFactor.z, mat.props.emissiveFactor.w};
    j["alphaProperties"] = {mat.props.alphaProperties.x, mat.props.alphaProperties.y, mat.props.alphaProperties.z, mat.props.alphaProperties.w};
    j["physicalProperties"] = {mat.props.physicalProperties.x, mat.props.physicalProperties.y, mat.props.physicalProperties.z, mat.props.physicalProperties.w};

    for (int i = 0; i < 6; ++i) {
        j["textureRefs"][i] = mat.textureRefs[i].id;
        const SamplerDesc& s = mat.samplerDesc[i];
        j["samplerDesc"][i] = {
            {"magFilter",      static_cast<int>(s.magFilter)},
            {"minFilter",      static_cast<int>(s.minFilter)},
            {"mipmapMode",     static_cast<int>(s.mipmapMode)},
            {"addressModeU",   static_cast<int>(s.addressModeU)},
            {"addressModeV",   static_cast<int>(s.addressModeV)},
            {"addressModeW",   static_cast<int>(s.addressModeW)},
            {"mipLodBias",     s.mipLodBias},
            {"minLod",         s.minLod},
            {"maxLod",         s.maxLod},
            {"anisotropyEnable", s.anisotropyEnable},
            {"maxAnisotropy",  s.maxAnisotropy},
        };
    }

    return j;
}

Material DeserializeMaterial(const nlohmann::json& j, const std::filesystem::path& sourcePath)
{
    Material mat{};
    mat.name = j["name"].get<Core::InlineString<128>>();
    mat.id = MaterialID(j["id"].get<uint64_t>());
    mat.pipelineID = StringID(j["pipeline"].get<uint64_t>());

    auto readF4 = [&](const char* key, glm::vec4& v) {
        const auto& a = j[key];
        v = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
    };
    auto readI4 = [&](const char* key, glm::ivec4& v) {
        const auto& a = j[key];
        v = {a[0].get<int32_t>(), a[1].get<int32_t>(), a[2].get<int32_t>(), a[3].get<int32_t>()};
    };

    MaterialProperties& p = mat.props;
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

    if (j.contains("textureRefs")) {
        for (int i = 0; i < 6; ++i) {
            mat.textureRefs[i] = TextureID(j["textureRefs"][i].get<uint64_t>());
        }
    }
    if (j.contains("samplerDesc")) {
        for (int i = 0; i < 6; ++i) {
            const auto& s = j["samplerDesc"][i];
            mat.samplerDesc[i].magFilter      = static_cast<VkFilter>(s["magFilter"].get<int>());
            mat.samplerDesc[i].minFilter      = static_cast<VkFilter>(s["minFilter"].get<int>());
            mat.samplerDesc[i].mipmapMode     = static_cast<VkSamplerMipmapMode>(s["mipmapMode"].get<int>());
            mat.samplerDesc[i].addressModeU   = static_cast<VkSamplerAddressMode>(s["addressModeU"].get<int>());
            mat.samplerDesc[i].addressModeV   = static_cast<VkSamplerAddressMode>(s["addressModeV"].get<int>());
            mat.samplerDesc[i].addressModeW   = static_cast<VkSamplerAddressMode>(s["addressModeW"].get<int>());
            mat.samplerDesc[i].mipLodBias     = s["mipLodBias"].get<float>();
            mat.samplerDesc[i].minLod         = s["minLod"].get<float>();
            mat.samplerDesc[i].maxLod         = s["maxLod"].get<float>();
            mat.samplerDesc[i].anisotropyEnable = s["anisotropyEnable"].get<uint32_t>();
            mat.samplerDesc[i].maxAnisotropy  = s["maxAnisotropy"].get<float>();
        }
    }

    mat.immutable = false;
    mat.sourcePath = sourcePath;

    return mat;
}
} // Engine
