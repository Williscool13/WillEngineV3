//
// Created by William on 2026-05-14.
//

#ifndef WILL_ENGINE_TEXT_MATERIAL_H
#define WILL_ENGINE_TEXT_MATERIAL_H

#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "engine/core/text_material_id.h"
#include "render/shaders/common_interop.h"

namespace Engine
{
struct TextMaterial
{
    TextMaterialID id{};
    Core::InlineString<128> name{};
    Core::Path sourcePath{};

    float4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
    float4 outlineColor{0.0f, 0.0f, 0.0f, 0.0f};
    float outlineWidth{0.0f};
    float shadowSoftness{1.0f};
    float2 shadowOffset{0.0f, 0.0f};
    float4 shadowColor{0.0f, 0.0f, 0.0f, 0.0f};
};

class TextWriter;
class TextReader;

void SerializeTextMaterial(const TextMaterial& mat, TextWriter& w);

TextMaterial DeserializeTextMaterial(const TextReader& r, const Core::Path& sourcePath);
} // Engine

#endif //WILL_ENGINE_TEXT_MATERIAL_H
