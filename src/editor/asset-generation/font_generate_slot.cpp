//
// Created by William on 2026-05-11.
//

#include "font_generate_slot.h"

#include <fstream>
#include <vector>

#include "core/containers/heap_array.h"
#include "core/containers/vector.h"

#include <volk.h>
#include <ktx.h>
#include <msdf-atlas-gen/msdf-atlas-gen.h>

#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/font/font_format.h"
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

void FontGenerateSlot::Launch(FontGenerateSlotHandle slotHandle, const Core::Path& _ttfPath, const Core::Path& _outputPath, Engine::FontID _fontId)
{
    _slotHandle = slotHandle;
    ttfPath = _ttfPath;
    outputPath = _outputPath;
    fontId = _fontId;

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

    ktxTexture2* ktxTex{nullptr};
    ktxTextureCreateInfo ktxInfo{};
    ktxInfo.vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    ktxInfo.baseWidth = static_cast<uint32_t>(atlasWidth);
    ktxInfo.baseHeight = static_cast<uint32_t>(atlasHeight);
    ktxInfo.baseDepth = 1;
    ktxInfo.numDimensions = 2;
    ktxInfo.numLevels = 1;
    ktxInfo.numLayers = 1;
    ktxInfo.numFaces = 1;
    ktxInfo.isArray = KTX_FALSE;
    ktxInfo.generateMipmaps = KTX_FALSE;

    if (ktxTexture2_Create(&ktxInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktxTex) != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to create KTX2 texture for font atlas");
        msdfgen::destroyFont(font);
        return false;
    }

    ktxTexture_SetImageFromMemory(ktxTexture(ktxTex), 0, 0, 0, rgba.Data(), rgba.Size());

    ktx_uint8_t* ktxBytes{nullptr};
    ktx_size_t ktxSize{0};
    if (ktxTexture2_WriteToMemory(ktxTex, &ktxBytes, &ktxSize) != KTX_SUCCESS) {
        LOG_ERROR(Asset, "Failed to serialise font atlas KTX2 to memory");
        ktxTexture_Destroy(ktxTexture(ktxTex));
        msdfgen::destroyFont(font);
        return false;
    }
    ktxTexture_Destroy(ktxTexture(ktxTex));

    const size_t compressMaxSize = Engine::CompressMaxSize(Engine::DEFAULT_FONT_COMPRESSION, ktxSize);
    Core::HeapArray<uint8_t> atlasCompressed(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator, compressMaxSize);
    const size_t compressedSize = Engine::Compress(Engine::DEFAULT_FONT_COMPRESSION, ktxBytes, ktxSize, atlasCompressed.Data(), compressMaxSize);
    free(ktxBytes);

    // Gather glyph metrics
    const msdf_atlas::FontGeometry::GlyphRange glyphRange = fontGeometry.getGlyphs();
    const msdfgen::FontMetrics& metrics = fontGeometry.getMetrics();
    const double emSize = metrics.emSize;
    const double invAtlasW = 1.0 / atlasWidth;
    const double invAtlasH = 1.0 / atlasHeight;

    Core::Vector<Engine::WGlyphInfo> glyphInfos(&memoryManager->AssetsScratch(), Core::AllocTag::AssetGenerator);
    glyphInfos.Reserve(glyphs.size());
    for (const msdf_atlas::GlyphGeometry& g : glyphs) {
        if (g.getCodepoint() == 0) { continue; }

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
    header.atlasUncompressedSize = static_cast<uint64_t>(ktxSize);

    const Core::InlineString<> stem = Core::InlineString(outputPath.Stem());
    const size_t copyLen = std::min(stem.Size(), Engine::WFONT_NAME_LENGTH - 1);
    memcpy(header.name, stem.c_str(), copyLen);
    header.name[copyLen] = '\0';

    Platform::CreateDirectories(outputPath.Parent().c_str());
    std::ofstream f(outputPath.c_str(), std::ios::binary);
    if (!f) {
        LOG_ERROR(Asset, "Failed to open font output file: {}", outputPath.c_str());
        return false;
    }

    if (!Engine::WriteWFontHeader(f, header)) {
        LOG_ERROR(Asset, "Failed to write font header: {}", outputPath.c_str());
        return false;
    }

    f.write(reinterpret_cast<const char*>(glyphInfos.Data()), static_cast<std::streamsize>(glyphInfos.Size() * sizeof(Engine::WGlyphInfo)));
    f.write(reinterpret_cast<const char*>(atlasCompressed.Data()), static_cast<std::streamsize>(compressedSize));

    LOG_INFO(Asset, "Wrote font {}", outputPath.c_str());
    return f.good();
}
} // namespace Editor
