//
// Created by William on 2025-12-12.
//

#include "render_interface.h"

namespace Core
{
ViewFamily::ViewFamily(TlsfAllocator& allocator)
    : allocator(&allocator)
{
    portalViews = FixedVector<PortalView>(&allocator, AllocTag::FrameSync, Render::VIEW_COUNT - 1);

    modelMatrices = Vector<Model>(&allocator, AllocTag::FrameSync, 256);

    instances = Vector<InstanceData>(&allocator, AllocTag::FrameSync, 128);
    worldGlyphQuads = Vector<WorldGlyphQuad>(&allocator, AllocTag::FrameSync, 256);
    textInstances = Vector<TextInstanceDataFull>(&allocator, AllocTag::FrameSync, 32);

    lightingBuckets = Map<StringID, uint32_t>(&allocator, AllocTag::FrameSync, 256);
    textDrawCalls = Vector<TextDrawCall>(&allocator, AllocTag::FrameSync, 256);

    activeMaterials = Map<Engine::MaterialID, uint32_t>(&allocator, AllocTag::FrameSync, 256);
    materials = Vector<Engine::RenderMaterial>(&allocator, AllocTag::FrameSync, 256);
    activeTextMaterials = Map<Engine::TextMaterialID, uint32_t>(&allocator, AllocTag::FrameSync, 256);
    textMaterials = Vector<TextRenderMaterial>(&allocator, AllocTag::FrameSync, 256);

    debugLines = Vector<DebugLine>(&allocator, AllocTag::FrameSync, 256);
    debugBoxes = Vector<DebugBox>(&allocator, AllocTag::FrameSync, 256);
    debugSpheres = Vector<DebugSphere>(&allocator, AllocTag::FrameSync, 256);
}

FrameBuffer::FrameBuffer(TlsfAllocator& allocator)
    : allocator(&allocator)
{
    mainViewFamily = ViewFamily(allocator);
    bufferAcquireOperations = Vector<BufferAcquireOperation>(&allocator, AllocTag::FrameSync, 256);
    imageAcquireOperations = Vector<ImageAcquireOperation>(&allocator, AllocTag::FrameSync, 256);
}
} // Core
