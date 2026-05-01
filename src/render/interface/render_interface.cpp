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
    mainPassInstances = Vector<InstanceData>(&allocator, AllocTag::FrameSync, 128);
    customShaderDraws = Map<StringID, CustomShaderDraw>(&allocator, AllocTag::FrameSync, 256);

    modelMatrices = Vector<Model>(&allocator, AllocTag::FrameSync, 256);
    activeMaterials = Map<Engine::MaterialID, uint32_t>(&allocator, AllocTag::FrameSync, 256);
    materials = Vector<Engine::RenderMaterial>(&allocator, AllocTag::FrameSync, 256);

    debugLines = Vector<DebugLine>(&allocator, AllocTag::FrameSync, 256);
    debugBoxes = Vector<DebugBox>(&allocator, AllocTag::FrameSync, 256);
    debugSpheres = Vector<DebugSphere>(&allocator, AllocTag::FrameSync, 256);
}

CustomShaderDraw& ViewFamily::GetOrCreateCustomShaderDraw(StringID id)
{
    CustomShaderDraw& customDraw = customShaderDraws[id];
    if (customDraw.instances.IsAllocated()) {
        customDraw.instances = Vector<InstanceData>(allocator, AllocTag::FrameSync);
    }

    return customDraw;
}

FrameBuffer::FrameBuffer(TlsfAllocator& allocator)
    : allocator(&allocator)
{
    mainViewFamily = ViewFamily(allocator);
    bufferAcquireOperations = Vector<BufferAcquireOperation>(&allocator, AllocTag::FrameSync, 256);
    imageAcquireOperations = Vector<ImageAcquireOperation>(&allocator, AllocTag::FrameSync, 256);
}
} // Core
