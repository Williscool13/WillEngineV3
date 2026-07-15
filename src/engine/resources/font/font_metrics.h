//
// Created by William on 2026-07-14.
//

#ifndef WILL_ENGINE_FONT_METRICS_H
#define WILL_ENGINE_FONT_METRICS_H

#include <cstdint>

#include "font.h"
#include "engine/asset_manager.h"

namespace Engine
{
/** Pixel advance for a single glyph at the given font size; falls back to a quarter-em box for missing glyphs/fonts. */
inline float GlyphAdvance(AssetManager* assetManager, FontHandle font, uint32_t codepoint, float fontSize)
{
    const Font* f = assetManager->GetFont(font);
    if (!f) { return fontSize * 0.25f; }
    const WGlyphInfo* g = assetManager->GetGlyph(font, codepoint);
    if (!g) { return fontSize * 0.25f; }
    return g->advance * (fontSize / f->header.emSize);
}

/** Pixel width of text[0, count); letterSpacing applied between glyphs, matching Clay's own measure-text convention. */
inline float MeasureText(AssetManager* assetManager, FontHandle font, const char* text, int32_t count, float fontSize, float letterSpacing = 0.0f)
{
    float width = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        width += GlyphAdvance(assetManager, font, static_cast<unsigned char>(text[i]), fontSize);
        if (letterSpacing != 0.0f && i < count - 1) { width += letterSpacing; }
    }
    return width;
}
} // Engine

#endif //WILL_ENGINE_FONT_METRICS_H
