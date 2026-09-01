//
// Created by William on 2026-05-12.
//

#ifndef WILL_ENGINE_FONT_H
#define WILL_ENGINE_FONT_H

#include "font_format.h"
#include "offsetAllocator.hpp"
#include "core/containers/heap_array.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "engine/asset_manager_types.h"
#include "engine/core/font_id.h"
#include "render/descriptors/vk_bindless_resources_sampler_images.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"

namespace Engine
{
struct Font
{
    enum class LoadState { NotLoaded, Loading, Loaded, FailedToLoad };

    FontHandle selfHandle{FontHandle::INVALID};
    FontID fontId{};
    Core::InlineString<128> name{};
    Core::Path source{};

    /** Header retained for runtime access: emSize, ascender, lineHeight, slug blob fields, etc. */
    WFontHeader header{};
    /** Glyph map loaded synchronously in LoadFont. Freed on unload. */
    Core::HeapArray<WGlyphInfo> glyphs{};

    /** Vector outline tables for 3D text, loaded synchronously when the font was imported with contours. Indexed parallel to glyphs. */
    Core::HeapArray<WGlyphContourRange> glyphContourRanges{};
    Core::HeapArray<WContourRange> contourRanges{};
    Core::HeapArray<WFontEdge> contourEdges{};

    /** Slug curve blob residency in megaFontCurveBuffer. Lifecycle owned by Font. */
    OffsetAllocator::Allocation curveAllocation{};
    /** 8-aligned byte offset of the blob inside megaFontCurveBuffer. */
    uint32_t curveByteOffset{0};

    /** Effects SDF atlas; only present when header.sdfUncompressedSize > 0, bindless handle reserved in LoadFont. */
    Render::AllocatedImage atlasImage{};
    Render::ImageView atlasImageView{};
    Render::BindlessTextureHandle atlasBindlessHandle{};
    Core::ImageAcquireOperation atlasAcquireBarrier{};

    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount{0};
    uint64_t retireFrame{0};
};
} // Engine

#endif //WILL_ENGINE_FONT_H
