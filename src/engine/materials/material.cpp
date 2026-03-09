//
// Created by William on 2026-03-09.
//

#include "material.h"

namespace Engine
{
MaterialID HashMaterial(const MaterialProperties& m)
{
    return MaterialID(fnv1a64(reinterpret_cast<const uint8_t*>(&m), sizeof(MaterialProperties)));
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

    return j;
}

Material DeserializeMaterial(const nlohmann::json& j, const std::filesystem::path& sourcePath)
{
    Material mat{};
    mat.name = j["name"].get<std::string>();
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


    mat.immutable = false;
    mat.sourcePath = sourcePath;

    return mat;
}
} // Engine
