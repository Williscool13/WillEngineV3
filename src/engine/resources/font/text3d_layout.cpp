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

    // wrapWidth is world units; layout runs in EM space ahead of the scale multiply.
    const float wrapLimit = (params.wrapWidth > 0.0f && params.scale > 0.0f) ? params.wrapWidth / params.scale : 0.0f;

    struct LineRange
    {
        size_t start;
        size_t end;
    };
    Core::InlineVector<LineRange, TEXT3D_MAX_GLYPHS + 1> lines;

    size_t lineStart = 0;
    while (lineStart <= len && !lines.IsFull()) {
        size_t hardEnd = lineStart;
        while (hardEnd < len && text[hardEnd] != '\n') { ++hardEnd; }

        size_t segStart = lineStart;
        if (wrapLimit > 0.0f) {
            float width = 0.0f;
            uint32_t glyphCount = 0;
            size_t lastSpace = SIZE_MAX;
            for (size_t i = segStart; i < hardEnd && !lines.IsFull(); ++i) {
                if (text[i] == ' ') { lastSpace = i; }
                const int32_t gi = FindGlyphIndex(font, static_cast<uint8_t>(text[i]));
                if (gi < 0) { continue; }
                const float adv = font.glyphs[gi].advance + (glyphCount > 0 ? params.tracking : 0.0f);
                if (width + adv > wrapLimit && lastSpace != SIZE_MAX && lastSpace > segStart) {
                    lines.PushBack({segStart, lastSpace});
                    segStart = lastSpace + 1;
                    i = segStart - 1;
                    width = 0.0f;
                    glyphCount = 0;
                    lastSpace = SIZE_MAX;
                    continue;
                }
                width += adv;
                ++glyphCount;
            }
        }
        if (!lines.IsFull()) { lines.PushBack({segStart, hardEnd}); }
        lineStart = hardEnd + 1;
    }

    const uint32_t lineCount = static_cast<uint32_t>(lines.Size());

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

    for (uint32_t li = 0; li < lineCount; ++li) {
        float lineWidth = 0.0f;
        uint32_t glyphCount = 0;
        for (size_t i = lines[li].start; i < lines[li].end; ++i) {
            const int32_t gi = FindGlyphIndex(font, static_cast<uint8_t>(text[i]));
            if (gi < 0) { continue; }
            lineWidth += font.glyphs[gi].advance;
            ++glyphCount;
        }
        // Tracking sits between glyphs; a trailing one would bias the measured width and skew center/right.
        if (glyphCount > 1) { lineWidth += params.tracking * static_cast<float>(glyphCount - 1); }

        float penX = -lineWidth * alignFactor;
        const float penY = baseY - static_cast<float>(li) * lineHeight;
        for (size_t i = lines[li].start; i < lines[li].end; ++i) {
            const int32_t gi = FindGlyphIndex(font, static_cast<uint8_t>(text[i]));
            if (gi < 0) { continue; }
            out.PushBack({static_cast<uint32_t>(gi), penX, penY});
            penX += font.glyphs[gi].advance + params.tracking;
        }
    }
}
} // Engine
