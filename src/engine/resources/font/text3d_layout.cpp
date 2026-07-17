//
// Created by William on 2026-07-17.
//

#include "text3d_layout.h"

#include <algorithm>

#include "font.h"

namespace Engine
{
int32_t FindGlyphIndex(const Font& font, uint32_t codepoint)
{
    const uint32_t count = std::min(static_cast<uint32_t>(font.glyphs.Size()), static_cast<uint32_t>(font.glyphContourRanges.Size()));
    for (uint32_t i = 0; i < count; ++i) {
        if (font.glyphs[i].codepoint == codepoint) { return static_cast<int32_t>(i); }
    }
    return -1;
}

void LayoutText3D(const Font& font, const Text3DParams& params, Text3DGlyphPlacements& out)
{
    const char* text = params.text.c_str();
    const size_t len = params.text.Size();

    float lineHeight = font.header.lineHeight;
    if (lineHeight <= 0.0f) { lineHeight = font.header.ascender - font.header.descender; }
    if (lineHeight <= 0.0f) { lineHeight = 1.0f; }

    uint32_t lineCount = 1;
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\n') { ++lineCount; }
    }

    // Block extents measured from the first line's baseline, which is y=0 under Baseline anchoring.
    const float blockTop = font.header.ascender;
    const float blockBottom = font.header.descender - static_cast<float>(lineCount - 1) * lineHeight;

    float baseY = 0.0f;
    switch (params.anchor) {
        case Text3DAnchor::Top: baseY = -blockTop; break;
        case Text3DAnchor::Center: baseY = -(blockTop + blockBottom) * 0.5f; break;
        case Text3DAnchor::Bottom: baseY = -blockBottom; break;
        case Text3DAnchor::Baseline: break;
    }

    float alignFactor = 0.0f;
    switch (params.align) {
        case Text3DAlign::Center: alignFactor = 0.5f; break;
        case Text3DAlign::Right: alignFactor = 1.0f; break;
        case Text3DAlign::Left: break;
    }

    size_t lineStart = 0;
    uint32_t lineIndex = 0;
    while (lineStart <= len) {
        size_t lineEnd = lineStart;
        while (lineEnd < len && text[lineEnd] != '\n') { ++lineEnd; }

        float lineWidth = 0.0f;
        uint32_t glyphCount = 0;
        for (size_t i = lineStart; i < lineEnd; ++i) {
            const int32_t gi = FindGlyphIndex(font, static_cast<uint8_t>(text[i]));
            if (gi < 0) { continue; }
            lineWidth += font.glyphs[gi].advance;
            ++glyphCount;
        }
        // Tracking sits between glyphs; a trailing one would bias the measured width and skew center/right.
        if (glyphCount > 1) { lineWidth += params.tracking * static_cast<float>(glyphCount - 1); }

        float penX = -lineWidth * alignFactor;
        const float penY = baseY - static_cast<float>(lineIndex) * lineHeight;
        for (size_t i = lineStart; i < lineEnd; ++i) {
            const int32_t gi = FindGlyphIndex(font, static_cast<uint8_t>(text[i]));
            if (gi < 0) { continue; }
            out.PushBack({static_cast<uint32_t>(gi), penX, penY});
            penX += font.glyphs[gi].advance + params.tracking;
        }

        ++lineIndex;
        lineStart = lineEnd + 1;
    }
}
} // Engine
