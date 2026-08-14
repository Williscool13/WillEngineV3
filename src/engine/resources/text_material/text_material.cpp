//
// Created by William on 2026-05-14.
//

#include "text_material.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Engine
{
void SerializeTextMaterial(const TextMaterial& mat, TextWriter& w)
{
    w.KeyStr("name", mat.name.View());
    w.Key("id", mat.id.id);
    w.Key("colorTint", mat.colorTint);
    w.Key("outlineColor", mat.outlineColor);
    w.Key("outlineWidth", mat.outlineWidth);
    w.Key("shadowSoftness", mat.shadowSoftness);
    w.Key("shadowOffset", mat.shadowOffset);
    w.Key("shadowColor", mat.shadowColor);
}

TextMaterial DeserializeTextMaterial(const TextReader& r, const Core::Path& sourcePath)
{
    TextMaterial mat{};
    r.Str("name", mat.name);
    mat.id = TextMaterialID(r.U64("id"));
    mat.sourcePath = sourcePath;

    mat.colorTint = r.Vec4("colorTint", mat.colorTint);
    mat.outlineColor = r.Vec4("outlineColor", mat.outlineColor);
    mat.outlineWidth = r.Float("outlineWidth", mat.outlineWidth);
    mat.shadowSoftness = r.Float("shadowSoftness", mat.shadowSoftness);
    mat.shadowOffset = r.Vec2("shadowOffset", mat.shadowOffset);
    mat.shadowColor = r.Vec4("shadowColor", mat.shadowColor);

    return mat;
}
} // Engine
