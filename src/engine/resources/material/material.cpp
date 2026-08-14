//
// Created by William on 2026-03-09.
//

#include "material.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

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
        uint64_t fragmentShaderID;
        uint64_t lightingShaderID;

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
    key.fragmentShaderID = m.fragmentShader.id;
    key.lightingShaderID = m.lightingShader.id;

    return MaterialID(fnv1a64(reinterpret_cast<const uint8_t*>(&key), sizeof(StableKey)));
}

void SerializeMaterial(const Material& mat, TextWriter& w)
{
    assert(!mat.immutable);
    static const SamplerDesc DEFAULT_SAMPLER{};

    w.KeyStr("name", mat.name.View());
    w.Key("id", mat.id.id);
    w.Key("fragmentShader", mat.fragmentShader.id);
    w.Key("lightingShader", mat.lightingShader.id);

    const MaterialProperties& p = mat.props;
    w.Key("colorFactor", p.colorFactor);
    w.Key("metalRoughFactors", p.metalRoughFactors);
    w.Key("colorUvTransform", p.colorUvTransform);
    w.Key("metalRoughUvTransform", p.metalRoughUvTransform);
    w.Key("normalUvTransform", p.normalUvTransform);
    w.Key("emissiveUvTransform", p.emissiveUvTransform);
    w.Key("occlusionUvTransform", p.occlusionUvTransform);
    w.Key("emissiveFactor", p.emissiveFactor);
    w.Key("alphaProperties", p.alphaProperties);
    w.Key("physicalProperties", p.physicalProperties);

    w.Count("samplers", 6);
    for (int32_t i = 0; i < 6; ++i) {
        const SamplerDesc& s = mat.samplerDesc[i];
        w.BeginBlock("s");
        w.KeyOpt("textureRef", mat.textureRefs[i].id, TextureID::INVALID.id);
        w.KeyOpt("magFilter", static_cast<int32_t>(s.magFilter), static_cast<int32_t>(DEFAULT_SAMPLER.magFilter));
        w.KeyOpt("minFilter", static_cast<int32_t>(s.minFilter), static_cast<int32_t>(DEFAULT_SAMPLER.minFilter));
        w.KeyOpt("mipmapMode", static_cast<int32_t>(s.mipmapMode), static_cast<int32_t>(DEFAULT_SAMPLER.mipmapMode));
        w.KeyOpt("addressModeU", static_cast<int32_t>(s.addressModeU), static_cast<int32_t>(DEFAULT_SAMPLER.addressModeU));
        w.KeyOpt("addressModeV", static_cast<int32_t>(s.addressModeV), static_cast<int32_t>(DEFAULT_SAMPLER.addressModeV));
        w.KeyOpt("addressModeW", static_cast<int32_t>(s.addressModeW), static_cast<int32_t>(DEFAULT_SAMPLER.addressModeW));
        w.KeyOpt("mipLodBias", s.mipLodBias, DEFAULT_SAMPLER.mipLodBias);
        w.KeyOpt("minLod", s.minLod, DEFAULT_SAMPLER.minLod);
        w.KeyOpt("maxLod", s.maxLod, DEFAULT_SAMPLER.maxLod);
        w.KeyOpt("anisotropyEnable", static_cast<uint32_t>(s.anisotropyEnable), static_cast<uint32_t>(DEFAULT_SAMPLER.anisotropyEnable));
        w.KeyOpt("maxAnisotropy", s.maxAnisotropy, DEFAULT_SAMPLER.maxAnisotropy);
        w.EndBlock();
    }
}

Material DeserializeMaterial(const TextReader& r, const Core::Path& sourcePath)
{
    Material mat{};
    r.Str("name", mat.name);
    mat.id = MaterialID(r.U64("id"));
    if (r.Has("fragmentShader")) { mat.fragmentShader = StringID(r.U64("fragmentShader")); }
    if (r.Has("lightingShader")) { mat.lightingShader = StringID(r.U64("lightingShader")); }

    MaterialProperties& p = mat.props;
    p.colorFactor = r.Vec4("colorFactor");
    p.metalRoughFactors = r.Vec4("metalRoughFactors");
    p.colorUvTransform = r.Vec4("colorUvTransform");
    p.metalRoughUvTransform = r.Vec4("metalRoughUvTransform");
    p.normalUvTransform = r.Vec4("normalUvTransform");
    p.emissiveUvTransform = r.Vec4("emissiveUvTransform");
    p.occlusionUvTransform = r.Vec4("occlusionUvTransform");
    p.emissiveFactor = r.Vec4("emissiveFactor");
    p.alphaProperties = r.Vec4("alphaProperties");
    p.physicalProperties = r.Vec4("physicalProperties");

    // Runtime properties
    p.textureImageIndices = {-1, -1, -1, -1};
    p.textureSamplerIndices = {-1, -1, -1, -1};
    p.textureImageIndices2 = {-1, -1, -1, -1};
    p.textureSamplerIndices2 = {-1, -1, -1, -1};

    int32_t slot = 0;
    r.ForEachRecord("samplers", [&](const TextReader& s) {
        if (slot >= 6) { return; }
        SamplerDesc& d = mat.samplerDesc[slot];
        mat.textureRefs[slot] = TextureID(s.U64("textureRef", TextureID::INVALID.id));
        d.magFilter = static_cast<VkFilter>(s.Int("magFilter", static_cast<int32_t>(d.magFilter)));
        d.minFilter = static_cast<VkFilter>(s.Int("minFilter", static_cast<int32_t>(d.minFilter)));
        d.mipmapMode = static_cast<VkSamplerMipmapMode>(s.Int("mipmapMode", static_cast<int32_t>(d.mipmapMode)));
        d.addressModeU = static_cast<VkSamplerAddressMode>(s.Int("addressModeU", static_cast<int32_t>(d.addressModeU)));
        d.addressModeV = static_cast<VkSamplerAddressMode>(s.Int("addressModeV", static_cast<int32_t>(d.addressModeV)));
        d.addressModeW = static_cast<VkSamplerAddressMode>(s.Int("addressModeW", static_cast<int32_t>(d.addressModeW)));
        d.mipLodBias = s.Float("mipLodBias", d.mipLodBias);
        d.minLod = s.Float("minLod", d.minLod);
        d.maxLod = s.Float("maxLod", d.maxLod);
        d.anisotropyEnable = s.UInt("anisotropyEnable", d.anisotropyEnable);
        d.maxAnisotropy = s.Float("maxAnisotropy", d.maxAnisotropy);
        ++slot;
    });

    mat.immutable = false;
    mat.sourcePath = sourcePath;

    return mat;
}
} // Engine
