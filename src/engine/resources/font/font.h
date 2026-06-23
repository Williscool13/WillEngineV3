//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_FONT_H
#define WILL_ENGINE_FONT_H

#include "font_format.h"
#include "core/containers/heap_array.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "engine/resources/texture/texture.h"

namespace Engine
{
struct Font
{
    enum class LoadState { NotLoaded, Loading, Loaded, FailedToLoad };

    FontHandle selfHandle{FontHandle::INVALID};
    FontID fontId{};
    Core::InlineString<128> name{};
    Core::Path source{};

    /** Header retained for runtime access: emSize, sdfSpread, ascender, lineHeight, etc. */
    WFontHeader header{};
    /** Glyph map loaded synchronously in LoadFont. Freed on unload. */
    Core::HeapArray<WGlyphInfo> glyphs{};

    /** Vector outline tables for 3D text, loaded synchronously when the font was imported with contours. Indexed parallel to glyphs. */
    Core::HeapArray<WGlyphContourRange> glyphContourRanges{};
    Core::HeapArray<WContourRange> contourRanges{};
    Core::HeapArray<WFontEdge> contourEdges{};

    /** Atlas texture; not registered in the texture name/ID maps. Lifecycle owned by Font. */
    Texture atlasTexture{};

    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount{0};
    uint64_t retireFrame{0};
};
} // Engine

#endif //WILL_ENGINE_FONT_H
