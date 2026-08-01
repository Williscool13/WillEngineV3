//
// Created by William on 2026-07-17.
//

#ifndef WILL_ENGINE_TEXT3D_LAYOUT_H
#define WILL_ENGINE_TEXT3D_LAYOUT_H

#include <cstddef>
#include <cstdint>

#include "core/containers/inline_vector.h"
#include "engine/resources/model/model_types.h"

namespace Engine
{
struct Font;

/** A glyph resolved to its place in the string. penX/penY are EM-space offsets to add to the glyph outline before scaling. */
struct Text3DPlacedGlyph
{
    uint32_t glyphIndex{0};
    float penX{0.0f};
    float penY{0.0f};
};

/** One placement per byte of Text3DParams::text is the worst case. */
inline constexpr size_t TEXT3D_MAX_GLYPHS = 256;
using Text3DGlyphPlacements = Core::InlineVector<Text3DPlacedGlyph, TEXT3D_MAX_GLYPHS>;

int32_t FindGlyphIndex(const Font& font, uint32_t codepoint);

/**
 * Resolves text into per-glyph pen positions, breaking lines on '\n' (plus word wrap at `wrapWidth`) and applying `align` and `anchor`.
 * Shared by the render mesh and the collider so the two can never disagree on placement.
 * @param out appended to, not cleared. Characters with no glyph in @p font are skipped and consume no advance.
 */
void LayoutText3D(const Font& font, const Text3DParams& params, Text3DGlyphPlacements& out);
} // Engine

#endif //WILL_ENGINE_TEXT3D_LAYOUT_H
