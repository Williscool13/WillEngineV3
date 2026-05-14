//
// Created by William on 2026-05-14.
//

#include "text_material.h"

#include "engine/serialization/serialization.h"

namespace Engine
{
nlohmann::json SerializeTextMaterial(const TextMaterial& mat)
{
    nlohmann::json j;
    j["name"] = mat.name;
    j["id"] = mat.id.id;
    j["colorTint"] = {mat.colorTint.x, mat.colorTint.y, mat.colorTint.z, mat.colorTint.w};
    j["outlineColor"] = {mat.outlineColor.x, mat.outlineColor.y, mat.outlineColor.z, mat.outlineColor.w};
    j["outlineWidth"] = mat.outlineWidth;
    j["shadowSoftness"] = mat.shadowSoftness;
    j["shadowOffset"] = {mat.shadowOffset.x, mat.shadowOffset.y};
    j["shadowColor"] = {mat.shadowColor.x, mat.shadowColor.y, mat.shadowColor.z, mat.shadowColor.w};
    return j;
}

TextMaterial DeserializeTextMaterial(const nlohmann::json& j, const Core::Path& sourcePath)
{
    TextMaterial mat{};
    mat.name = j["name"].get<Core::InlineString<128>>();
    mat.id = TextMaterialID(j["id"].get<uint64_t>());
    mat.sourcePath = sourcePath;

    auto readF4 = [&](const char* key, float4& v) {
        const auto& a = j[key];
        v = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
    };
    auto readF2 = [&](const char* key, float2& v) {
        const auto& a = j[key];
        v = {a[0].get<float>(), a[1].get<float>()};
    };

    readF4("colorTint", mat.colorTint);
    readF4("outlineColor", mat.outlineColor);
    mat.outlineWidth = j["outlineWidth"].get<float>();
    mat.shadowSoftness = j["shadowSoftness"].get<float>();
    readF2("shadowOffset", mat.shadowOffset);
    readF4("shadowColor", mat.shadowColor);

    return mat;
}
} // Engine
