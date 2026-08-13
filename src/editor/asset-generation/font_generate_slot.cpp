//
// Created by William on 2026-05-11.
//

#include "font_generate_slot.h"

#include <vector>

#include "core/containers/heap_array.h"
#include "core/containers/vector.h"

#include <volk.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>

#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/font/font_format.h"
#include "engine/resources/wimage_format.h"
#include "engine/serialization/serialization.h"
#include "platform/file_utils.h"

namespace Editor
{
FontGenerateSlot::~FontGenerateSlot()
{
    if (ft) {
        msdfgen::deinitializeFreetype(ft);
        ft = nullptr;
    }
}

void FontGenerateSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(bool success, FontGenerateSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    memoryManager = _memoryManager;
    _notifyCallback = std::move(notifyCallback);
    ft = msdfgen::initializeFreetype();
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
    if (!ft) {
        LOG_ERROR(Asset, "FreeType not initialized");
        return false;
    }

    msdfgen::FontHandle* font = msdfgen::loadFont(ft, ttfPath.c_str());
    if (!font) {
        LOG_ERROR(Asset, "Failed to load font: {}", ttfPath.c_str());
        return false;
    }

    msdf_atlas::Charset charset = msdf_atlas::Charset::ASCII;

    // MEM: std::vector forced by msdf-atlas-gen API; FontGeometry takes std::vector<GlyphGeometry>* by pointer and stores it internally. No workaround without forking.
    // MEM: msdfgen (shape/contour/edge data) and msdf-atlas-gen (BitmapAtlasStorage float atlas) both use new/delete internally; no allocator hook is exposed.
    // MEM: FreeType allocator cannot be hooked through msdfgen's opaque FreetypeHandle; initializeFreetype() is the only constructor.
    std::vector<msdf_atlas::GlyphGeometry> glyphs;
    msdf_atlas::FontGeometry fontGeometry(&glyphs);
    fontGeometry.loadCharset(font, 1.0, charset);

    constexpr double MSDF_PIXEL_RANGE = 8.0;
    constexpr double MSDF_MIN_SCALE = 48.0;

    for (msdf_atlas::GlyphGeometry& glyph : glyphs) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0);
    }

    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::POWER_OF_TWO_RECTANGLE);
    packer.setMinimumScale(MSDF_MIN_SCALE);
    packer.setPixelRange(MSDF_PIXEL_RANGE);
    packer.setMiterLimit(1.0);
    packer.pack(glyphs.data(), static_cast<int>(glyphs.size()));

    int atlasWidth = 0;
    int atlasHeight = 0;
    packer.getDimensions(atlasWidth, atlasHeight);

    msdf_atlas::GeneratorAttributes genAttribs;
    genAttribs.config.overlapSupport = true;
    genAttribs.scanlinePass = true;

    auto generator = msdf_atlas::ImmediateAtlasGenerator<float, 4, msdf_atlas::mtsdfGenerator, msdf_atlas::BitmapAtlasStorage<float, 4> >(atlasWidth, atlasHeight);
    generator.setAttributes(genAttribs);
    generator.setThreadCount(4);
    generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

    auto bitmap = static_cast<msdfgen::BitmapConstRef<float, 4>>(generator.atlasStorage());

    // V-flip baked here for Vulkan y-down.
    const size_t rgbaSize = static_cast<size_t>(atlasWidth) * atlasHeight * 4;
    Core::HeapArray<uint8_t> rgba(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, rgbaSize);
    for (int y = 0; y < atlasHeight; ++y) {
        const int srcY = (atlasHeight - 1 - y);
        for (int x = 0; x < atlasWidth; ++x) {
            const float* src = bitmap(x, srcY);
            uint8_t* dst = rgba.Data() + (y * atlasWidth + x) * 4;
            dst[0] = static_cast<uint8_t>(std::clamp(src[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[1] = static_cast<uint8_t>(std::clamp(src[1], 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[2] = static_cast<uint8_t>(std::clamp(src[2], 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[3] = static_cast<uint8_t>(std::clamp(src[3], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }

    const Engine::WImageDesc desc{VK_FORMAT_R8G8B8A8_UNORM, static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight), 1, 1};
    const size_t blobSize = Engine::WImageBlobSize(desc);
    Core::HeapArray<uint8_t> blob(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, blobSize);
    if (Engine::WImageBlobInit(blob.Data(), blob.Size(), desc) == 0) {
        LOG_ERROR(Asset, "Failed to lay out font atlas image blob");
        msdfgen::destroyFont(font);
        return false;
    }
    memcpy(Engine::WImageFaceData(blob.Data(), 0, 0), rgba.Data(), rgba.Size());

    const size_t compressMaxSize = Engine::CompressMaxSize(Engine::DEFAULT_FONT_COMPRESSION, blobSize);
    Core::HeapArray<uint8_t> atlasCompressed(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, compressMaxSize);
    const size_t compressedSize = Engine::Compress(Engine::DEFAULT_FONT_COMPRESSION, blob.Data(), blobSize, atlasCompressed.Data(), compressMaxSize);

    // Gather glyph metrics
    const msdf_atlas::FontGeometry::GlyphRange glyphRange = fontGeometry.getGlyphs();
    const msdfgen::FontMetrics& metrics = fontGeometry.getMetrics();
    const double emSize = metrics.emSize;
    const double invAtlasW = 1.0 / atlasWidth;
    const double invAtlasH = 1.0 / atlasHeight;

    Core::Vector<Engine::WGlyphInfo> glyphInfos(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    glyphInfos.Reserve(glyphs.size());

    // Vector outlines for 3D text. Built parallel to glyphInfos so codepoint -> glyph index -> contour range stays aligned.
    Core::Vector<Engine::WGlyphContourRange> glyphContourRanges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<Engine::WContourRange> contourRanges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<Engine::WFontEdge> contourEdges(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);

    for (const msdf_atlas::GlyphGeometry& g : glyphs) {
        if (g.getCodepoint() == 0) { continue; }

        const double geometryScale = g.getGeometryScale();
        Engine::WGlyphContourRange gcr{};
        gcr.firstContour = static_cast<uint32_t>(contourRanges.Size());
        const msdfgen::Shape& shape = g.getShape();
        for (const msdfgen::Contour& contour : shape.contours) {
            Engine::WContourRange cr{};
            cr.firstEdge = static_cast<uint32_t>(contourEdges.Size());
            for (const msdfgen::EdgeHolder& holder : contour.edges) {
                const msdfgen::EdgeSegment* seg = holder;
                const msdfgen::Point2* cp = seg->controlPoints();
                Engine::WFontEdge edge{};
                int pointCount = 2;
                switch (seg->type()) {
                    case msdfgen::QuadraticSegment::EDGE_TYPE: edge.kind = Engine::WFontEdgeKind::Quadratic; pointCount = 3; break;
                    case msdfgen::CubicSegment::EDGE_TYPE: edge.kind = Engine::WFontEdgeKind::Cubic; pointCount = 4; break;
                    default: edge.kind = Engine::WFontEdgeKind::Linear; pointCount = 2; break;
                }
                for (int i = 0; i < pointCount; ++i) {
                    edge.p[i * 2] = static_cast<float>(cp[i].x * geometryScale);
                    edge.p[i * 2 + 1] = static_cast<float>(cp[i].y * geometryScale);
                }
                contourEdges.PushBack(edge);
            }
            cr.edgeCount = static_cast<uint32_t>(contourEdges.Size()) - cr.firstEdge;
            contourRanges.PushBack(cr);
        }
        gcr.contourCount = static_cast<uint32_t>(contourRanges.Size()) - gcr.firstContour;
        glyphContourRanges.PushBack(gcr);

        Engine::WGlyphInfo info{};
        info.codepoint = static_cast<uint32_t>(g.getCodepoint());
        info.advance = static_cast<float>(g.getAdvance());

        double pl, pb, pr, pt;
        g.getQuadPlaneBounds(pl, pb, pr, pt);
        info.planeLeft = static_cast<float>(pl);
        info.planeBottom = static_cast<float>(pb);
        info.planeRight = static_cast<float>(pr);
        info.planeTop = static_cast<float>(pt);

        double al, ab, ar, at;
        g.getQuadAtlasBounds(al, ab, ar, at);
        // V-flip: atlas is y-up, Vulkan is y-down; baked here so runtime needs no flip
        info.uvLeft = static_cast<float>(al * invAtlasW);
        info.uvBottom = static_cast<float>((atlasHeight - at) * invAtlasH);
        info.uvRight = static_cast<float>(ar * invAtlasW);
        info.uvTop = static_cast<float>((atlasHeight - ab) * invAtlasH);

        glyphInfos.PushBack(info);
    }

    msdfgen::destroyFont(font);

    // Build and write .wsfont
    Engine::WFontHeader header{};
    header.fontId = fontId.id;
    header.contentVersion = contentVersion;
    header.major = Engine::FONT_MAJOR_VERSION;
    header.minor = Engine::FONT_MINOR_VERSION;
    header.sourceSizePx = static_cast<uint32_t>(MSDF_MIN_SCALE);
    header.sdfSpread = static_cast<uint32_t>(MSDF_PIXEL_RANGE);
    header.emSize = static_cast<float>(emSize);
    header.ascender = static_cast<float>(metrics.ascenderY);
    header.descender = static_cast<float>(metrics.descenderY);
    header.lineHeight = static_cast<float>(metrics.lineHeight);
    header.atlasWidth = static_cast<uint32_t>(atlasWidth);
    header.atlasHeight = static_cast<uint32_t>(atlasHeight);
    header.glyphCount = static_cast<uint32_t>(glyphInfos.Size());
    header.atlasDataSize = static_cast<uint64_t>(compressedSize);
    header.atlasUncompressedSize = static_cast<uint64_t>(blobSize);
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
    Engine::AppendRaw(out, atlasCompressed.Data(), compressedSize);

    if (!Platform::WriteFile(outputPath, out.Data(), out.Size())) {
        LOG_ERROR(Asset, "Failed to write font output file: {}", outputPath.c_str());
        return false;
    }

    LOG_INFO(Asset, "Wrote font {}", outputPath.c_str());
    return true;
}
} // namespace Editor
