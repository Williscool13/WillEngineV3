//
// Created by William on 2026-05-11.
//

#include "font_generate_slot.h"

#include <hb.h>
#include <hb-gpu.h>

#include <cmath>

#include "core/containers/heap_array.h"
#include "core/containers/vector.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/font/font_format.h"
#include "engine/serialization/serialization.h"
#include "platform/file_utils.h"
#include "render/shaders/text_interop.h"
#include "tracy/Tracy.hpp"

namespace Editor
{
static constexpr uint32_t CHARSET_FIRST = 0x20;
static constexpr uint32_t CHARSET_LAST = 0x7E;

struct SdfSegment
{
    float ax, ay, bx, by;
};

static void FlattenEdgeForSdf(const Engine::WFontEdge& e, Core::Vector<SdfSegment>& out)
{
    if (e.kind == Engine::WFontEdgeKind::Linear) {
        out.PushBack({e.p[0], e.p[1], e.p[2], e.p[3]});
        return;
    }
    const int n = e.kind == Engine::WFontEdgeKind::Quadratic ? 8 : 12;
    float px = e.p[0];
    float py = e.p[1];
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float u = 1.0f - t;
        float x, y;
        if (e.kind == Engine::WFontEdgeKind::Quadratic) {
            x = u * u * e.p[0] + 2.0f * u * t * e.p[2] + t * t * e.p[4];
            y = u * u * e.p[1] + 2.0f * u * t * e.p[3] + t * t * e.p[5];
        } else {
            x = u * u * u * e.p[0] + 3.0f * u * u * t * e.p[2] + 3.0f * u * t * t * e.p[4] + t * t * t * e.p[6];
            y = u * u * u * e.p[1] + 3.0f * u * u * t * e.p[3] + 3.0f * u * t * t * e.p[5] + t * t * t * e.p[7];
        }
        out.PushBack({px, py, x, y});
        px = x;
        py = y;
    }
}

struct ContourCapture
{
    Core::Vector<Engine::WContourRange>* contourRanges{nullptr};
    Core::Vector<Engine::WFontEdge>* edges{nullptr};
    uint32_t firstEdge{0};
    float startX{0.0f};
    float startY{0.0f};
    float curX{0.0f};
    float curY{0.0f};
    bool bOpen{false};
};

static void CaptureFinishContour(ContourCapture& c)
{
    if (!c.bOpen) { return; }
    if (c.curX != c.startX || c.curY != c.startY) {
        Engine::WFontEdge edge{};
        edge.kind = Engine::WFontEdgeKind::Linear;
        edge.p[0] = c.curX;
        edge.p[1] = c.curY;
        edge.p[2] = c.startX;
        edge.p[3] = c.startY;
        c.edges->PushBack(edge);
    }
    const uint32_t edgeCount = static_cast<uint32_t>(c.edges->Size()) - c.firstEdge;
    if (edgeCount > 0) {
        c.contourRanges->PushBack({c.firstEdge, edgeCount});
    }
    c.bOpen = false;
}

static void CaptureMoveTo(hb_draw_funcs_t*, void* drawData, hb_draw_state_t*, float toX, float toY, void*)
{
    auto& c = *static_cast<ContourCapture*>(drawData);
    CaptureFinishContour(c);
    c.firstEdge = static_cast<uint32_t>(c.edges->Size());
    c.startX = c.curX = toX;
    c.startY = c.curY = toY;
    c.bOpen = true;
}

static void CaptureLineTo(hb_draw_funcs_t*, void* drawData, hb_draw_state_t*, float toX, float toY, void*)
{
    auto& c = *static_cast<ContourCapture*>(drawData);
    Engine::WFontEdge edge{};
    edge.kind = Engine::WFontEdgeKind::Linear;
    edge.p[0] = c.curX;
    edge.p[1] = c.curY;
    edge.p[2] = toX;
    edge.p[3] = toY;
    c.edges->PushBack(edge);
    c.curX = toX;
    c.curY = toY;
}

static void CaptureQuadraticTo(hb_draw_funcs_t*, void* drawData, hb_draw_state_t*, float cX, float cY, float toX, float toY, void*)
{
    auto& c = *static_cast<ContourCapture*>(drawData);
    Engine::WFontEdge edge{};
    edge.kind = Engine::WFontEdgeKind::Quadratic;
    edge.p[0] = c.curX;
    edge.p[1] = c.curY;
    edge.p[2] = cX;
    edge.p[3] = cY;
    edge.p[4] = toX;
    edge.p[5] = toY;
    c.edges->PushBack(edge);
    c.curX = toX;
    c.curY = toY;
}

static void CaptureCubicTo(hb_draw_funcs_t*, void* drawData, hb_draw_state_t*, float c1X, float c1Y, float c2X, float c2Y, float toX, float toY, void*)
{
    auto& c = *static_cast<ContourCapture*>(drawData);
    Engine::WFontEdge edge{};
    edge.kind = Engine::WFontEdgeKind::Cubic;
    edge.p[0] = c.curX;
    edge.p[1] = c.curY;
    edge.p[2] = c1X;
    edge.p[3] = c1Y;
    edge.p[4] = c2X;
    edge.p[5] = c2Y;
    edge.p[6] = toX;
    edge.p[7] = toY;
    c.edges->PushBack(edge);
    c.curX = toX;
    c.curY = toY;
}

static void CaptureClosePath(hb_draw_funcs_t*, void* drawData, hb_draw_state_t*, void*)
{
    CaptureFinishContour(*static_cast<ContourCapture*>(drawData));
}

void FontGenerateSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(bool success, FontGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    memoryManager = _memoryManager;
    _notifyCallback = std::move(notifyCallback);
}

void FontGenerateSlot::Launch(FontGenerateSlotHandle slotHandle, const Core::Path& _ttfPath, const Core::Path& _outputPath, Engine::FontID _fontId, uint64_t _contentVersion)
{
    _slotHandle = slotHandle;
    ttfPath = _ttfPath;
    outputPath = _outputPath;
    fontId = _fontId;
    contentVersion = _contentVersion;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }

    task.taskSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void FontGenerateSlot::Clear()
{
    ttfPath = Core::Path{};
    outputPath = Core::Path{};
    fontId = Engine::FontID{};
}

void FontGenerateSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    bool success = taskSlot->GenerateAndWrite();
    taskSlot->_notifyCallback(success, taskSlot->_slotHandle);
}

bool FontGenerateSlot::GenerateAndWrite()
{
    ZoneScopedN("SlugEncodeFont");

    Platform::ScopedFileMapping map(ttfPath);
    if (!map.data) {
        LOG_ERROR(Asset, "Failed to open font source: {}", ttfPath.c_str());
        return false;
    }

    hb_blob_t* fileBlob = hb_blob_create(reinterpret_cast<const char*>(map.data), static_cast<unsigned>(map.size), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(fileBlob, 0);
    hb_blob_destroy(fileBlob);
    if (hb_face_get_glyph_count(face) == 0) {
        LOG_ERROR(Asset, "Failed to load font face: {}", ttfPath.c_str());
        hb_face_destroy(face);
        return false;
    }

    // Default hb_font scale is upem, so every coordinate below is in font units.
    hb_font_t* font = hb_font_create(face);
    const uint32_t upem = hb_face_get_upem(face);

    hb_font_extents_t fontExtents{};
    if (!hb_font_get_h_extents(font, &fontExtents)) {
        LOG_WARN(Asset, "Font has no horizontal extents, using upem defaults: {}", ttfPath.c_str());
        fontExtents.ascender = static_cast<hb_position_t>(upem);
        fontExtents.descender = 0;
        fontExtents.line_gap = 0;
    }

    hb_gpu_draw_t* gpuDraw = hb_gpu_draw_create_or_fail();
    if (!gpuDraw) {
        LOG_ERROR(Asset, "Failed to create Slug encoder for {}", ttfPath.c_str());
        hb_font_destroy(font);
        hb_face_destroy(face);
        return false;
    }

    hb_draw_funcs_t* contourFuncs = hb_draw_funcs_create();
    hb_draw_funcs_set_move_to_func(contourFuncs, CaptureMoveTo, nullptr, nullptr);
    hb_draw_funcs_set_line_to_func(contourFuncs, CaptureLineTo, nullptr, nullptr);
    hb_draw_funcs_set_quadratic_to_func(contourFuncs, CaptureQuadraticTo, nullptr, nullptr);
    hb_draw_funcs_set_cubic_to_func(contourFuncs, CaptureCubicTo, nullptr, nullptr);
    hb_draw_funcs_set_close_path_func(contourFuncs, CaptureClosePath, nullptr, nullptr);

    Core::Vector<Engine::WGlyphInfo> glyphInfos(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<Engine::WGlyphContourRange> glyphContourRanges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<Engine::WContourRange> contourRanges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<Engine::WFontEdge> contourEdges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<uint8_t> slugTexels(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);

    bool bEncodeFailed = false;
    for (uint32_t cp = CHARSET_FIRST; cp <= CHARSET_LAST; ++cp) {
        hb_codepoint_t glyph = 0;
        if (!hb_font_get_nominal_glyph(font, cp, &glyph)) { continue; }

        Engine::WGlyphContourRange gcr{};
        gcr.firstContour = static_cast<uint32_t>(contourRanges.Size());
        ContourCapture capture{&contourRanges, &contourEdges};
        hb_font_draw_glyph(font, glyph, contourFuncs, &capture);
        CaptureFinishContour(capture);
        gcr.contourCount = static_cast<uint32_t>(contourRanges.Size()) - gcr.firstContour;
        glyphContourRanges.PushBack(gcr);

        hb_gpu_draw_glyph(gpuDraw, font, glyph);
        hb_glyph_extents_t extents{};
        hb_blob_t* encoded = hb_gpu_draw_encode(gpuDraw, &extents);
        if (!encoded) {
            LOG_ERROR(Asset, "Slug encode failed for codepoint {:#x} in {}", cp, ttfPath.c_str());
            bEncodeFailed = true;
            break;
        }

        unsigned encodedLength = 0;
        const char* encodedData = hb_blob_get_data(encoded, &encodedLength);

        Engine::WGlyphInfo info{};
        info.codepoint = cp;
        info.advance = static_cast<float>(hb_font_get_glyph_h_advance(font, glyph));
        info.planeLeft = static_cast<float>(extents.x_bearing);
        info.planeTop = static_cast<float>(extents.y_bearing);
        info.planeRight = static_cast<float>(extents.x_bearing + extents.width);
        info.planeBottom = static_cast<float>(extents.y_bearing + extents.height);
        info.slugTexelOffset = static_cast<uint32_t>(slugTexels.Size() / 8);
        info.slugTexelCount = encodedLength / 8;

        if (encodedLength > 0) {
            const size_t oldSize = slugTexels.Size();
            slugTexels.Resize(oldSize + encodedLength);
            memcpy(slugTexels.Data() + oldSize, encodedData, encodedLength);
        }
        hb_gpu_draw_recycle_blob(gpuDraw, encoded);

        glyphInfos.PushBack(info);
    }

    hb_draw_funcs_destroy(contourFuncs);
    hb_gpu_draw_destroy(gpuDraw);
    hb_font_destroy(font);
    hb_face_destroy(face);

    if (bEncodeFailed) { return false; }
    if (glyphInfos.IsEmpty()) {
        LOG_ERROR(Asset, "Font has no charset glyphs: {}", ttfPath.c_str());
        return false;
    }

    const size_t slugUncompressed = slugTexels.Size();
    size_t compressedSize = 0;
    Core::HeapArray<uint8_t> slugCompressed{};
    if (slugUncompressed > 0) {
        const size_t compressMaxSize = Engine::CompressMaxSize(Engine::DEFAULT_FONT_COMPRESSION, slugUncompressed);
        slugCompressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, compressMaxSize);
        compressedSize = Engine::Compress(Engine::DEFAULT_FONT_COMPRESSION, slugTexels.Data(), slugUncompressed, slugCompressed.Data(), compressMaxSize);
    }

    const uint32_t sdfCols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(glyphInfos.Size()))));
    const uint32_t sdfRows = (static_cast<uint32_t>(glyphInfos.Size()) + sdfCols - 1) / sdfCols;
    const uint32_t atlasW = sdfCols * Engine::FONT_SDF_CELL_PX;
    const uint32_t atlasH = sdfRows * Engine::FONT_SDF_CELL_PX;
    const float spreadFU = SDF_SPREAD_EM * static_cast<float>(upem);
    Core::HeapArray<uint8_t> sdfAtlas(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, static_cast<size_t>(atlasW) * atlasH);
    memset(sdfAtlas.Data(), 0, sdfAtlas.Size());
    {
        ZoneScopedN("SdfAtlasBake");
        Core::Vector<SdfSegment> segs(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
        for (uint32_t gi = 0; gi < glyphInfos.Size(); ++gi) {
            const Engine::WGlyphContourRange& gcr = glyphContourRanges[gi];
            if (gcr.contourCount == 0) { continue; }

            segs.Clear();
            for (uint32_t c = 0; c < gcr.contourCount; ++c) {
                const Engine::WContourRange& cr = contourRanges[gcr.firstContour + c];
                for (uint32_t e = 0; e < cr.edgeCount; ++e) {
                    FlattenEdgeForSdf(contourEdges[cr.firstEdge + e], segs);
                }
            }
            if (segs.IsEmpty()) { continue; }

            const Engine::WGlyphInfo& info = glyphInfos[gi];
            const float winMinX = info.planeLeft - spreadFU;
            const float winMaxX = info.planeRight + spreadFU;
            const float winMinY = info.planeBottom - spreadFU;
            const float winMaxY = info.planeTop + spreadFU;
            const uint32_t cellX = (gi % sdfCols) * Engine::FONT_SDF_CELL_PX;
            const uint32_t cellY = (gi / sdfCols) * Engine::FONT_SDF_CELL_PX;

            for (uint32_t ty = 0; ty < Engine::FONT_SDF_CELL_PX; ++ty) {
                const float py = winMaxY - (static_cast<float>(ty) + 0.5f) / static_cast<float>(Engine::FONT_SDF_CELL_PX) * (winMaxY - winMinY);
                uint8_t* row = sdfAtlas.Data() + static_cast<size_t>(cellY + ty) * atlasW + cellX;
                for (uint32_t tx = 0; tx < Engine::FONT_SDF_CELL_PX; ++tx) {
                    const float px = winMinX + (static_cast<float>(tx) + 0.5f) / static_cast<float>(Engine::FONT_SDF_CELL_PX) * (winMaxX - winMinX);

                    float d2 = 3.4e38f;
                    int winding = 0;
                    for (const SdfSegment& s : segs) {
                        const float ex = s.bx - s.ax;
                        const float ey = s.by - s.ay;
                        const float qx = px - s.ax;
                        const float qy = py - s.ay;
                        const float len2 = ex * ex + ey * ey;
                        float t = len2 > 1.0e-12f ? (qx * ex + qy * ey) / len2 : 0.0f;
                        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                        const float dx = qx - ex * t;
                        const float dy = qy - ey * t;
                        const float dd = dx * dx + dy * dy;
                        if (dd < d2) { d2 = dd; }

                        if ((s.ay > py) != (s.by > py)) {
                            const float xc = s.ax + (py - s.ay) / (s.by - s.ay) * ex;
                            if (xc > px) { winding += s.by > s.ay ? 1 : -1; }
                        }
                    }

                    const float sd = winding != 0 ? std::sqrt(d2) : -std::sqrt(d2);
                    float v = 0.5f + 0.5f * sd / spreadFU;
                    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                    row[tx] = static_cast<uint8_t>(v * 255.0f + 0.5f);
                }
            }
        }
    }

    const size_t sdfUncompressed = sdfAtlas.Size();
    size_t sdfCompressedSize = 0;
    Core::HeapArray<uint8_t> sdfCompressed{};
    if (sdfUncompressed > 0) {
        const size_t compressMaxSize = Engine::CompressMaxSize(Engine::DEFAULT_FONT_COMPRESSION, sdfUncompressed);
        sdfCompressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, compressMaxSize);
        sdfCompressedSize = Engine::Compress(Engine::DEFAULT_FONT_COMPRESSION, sdfAtlas.Data(), sdfUncompressed, sdfCompressed.Data(), compressMaxSize);
    }

    Engine::WFontHeader header{};
    header.fontId = fontId.id;
    header.contentVersion = contentVersion;
    header.major = Engine::FONT_MAJOR_VERSION;
    header.minor = Engine::FONT_MINOR_VERSION;
    header.emSize = static_cast<float>(upem);
    header.ascender = static_cast<float>(fontExtents.ascender);
    header.descender = static_cast<float>(fontExtents.descender);
    header.lineHeight = static_cast<float>(fontExtents.ascender - fontExtents.descender + fontExtents.line_gap);
    header.glyphCount = static_cast<uint32_t>(glyphInfos.Size());
    header.slugTexelCount = slugUncompressed / 8;
    header.slugDataSize = compressedSize;
    header.slugUncompressedSize = slugUncompressed;
    header.sdfCellPx = Engine::FONT_SDF_CELL_PX;
    header.sdfCols = sdfCols;
    header.sdfRows = sdfRows;
    header.sdfSpread = SDF_SPREAD_EM;
    header.sdfDataSize = sdfCompressedSize;
    header.sdfUncompressedSize = sdfUncompressed;
    header.contourGlyphCount = static_cast<uint32_t>(glyphContourRanges.Size());
    header.contourCount = static_cast<uint32_t>(contourRanges.Size());
    header.edgeCount = static_cast<uint32_t>(contourEdges.Size());

    const Core::InlineString<> stem = Core::InlineString(outputPath.Stem());
    const size_t copyLen = std::min(stem.Size(), Engine::WFONT_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    Core::Vector<std::byte> out(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Engine::WriteWFontHeader(out, header);
    Engine::AppendRaw(out, glyphInfos.Data(), glyphInfos.Size() * sizeof(Engine::WGlyphInfo));
    Engine::AppendRaw(out, glyphContourRanges.Data(), glyphContourRanges.Size() * sizeof(Engine::WGlyphContourRange));
    Engine::AppendRaw(out, contourRanges.Data(), contourRanges.Size() * sizeof(Engine::WContourRange));
    Engine::AppendRaw(out, contourEdges.Data(), contourEdges.Size() * sizeof(Engine::WFontEdge));
    if (compressedSize > 0) {
        Engine::AppendRaw(out, slugCompressed.Data(), compressedSize);
    }
    if (sdfCompressedSize > 0) {
        Engine::AppendRaw(out, sdfCompressed.Data(), sdfCompressedSize);
    }

    if (!Platform::WriteFile(outputPath, out.Data(), out.Size())) {
        LOG_ERROR(Asset, "Failed to write font output file: {}", outputPath.c_str());
        return false;
    }

    LOG_INFO(Asset, "Wrote font {}", outputPath.c_str());
    return true;
}
} // namespace Editor
