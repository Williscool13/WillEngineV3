//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_TYPES_H
#define WILL_ENGINE_RENDER_TYPES_H
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../interface/render_interface.h"
#include "core/containers/inline_map.h"
#include "render/shaders/common_interop.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj);

bool IntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius);

bool IntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max);

bool IntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation);

int32_t GetSphereSegments(const glm::vec3& center, const glm::vec3& viewPos, float radius);

/** Packs [0,1] RGBA to 8-bit unorm (round half up, matching the historical inline packs bit-exactly). */
inline uint32_t PackColorRGBA8(const glm::vec4& c)
{
    return (static_cast<uint32_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f)) |
           (static_cast<uint32_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
           (static_cast<uint32_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
           (static_cast<uint32_t>(glm::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

/** RGB variant with alpha forced to 255. */
inline uint32_t PackColorRGB8(const glm::vec3& c)
{
    return PackColorRGBA8(glm::vec4(c, 1.0f));
}

struct BucketIndices
{
    uint32_t shadingBucket;
    uint32_t lightingBucket;
};

struct RenderFamilyProperties
{
    Core::ViewFamily* viewFamily{nullptr};

    bool bCanRender{false};
    bool bWireframe{false};
    bool bOcclusionCulling{true};
    bool bOcclusionFreeze{false};
    uint32_t cullFlags{0xFFFFFFFFu};

    size_t modelBufferSize{128};
    size_t materialBufferSize{128};
    size_t shadeDispatchBufferSize{128};
    size_t lightingDispatchBufferSize{128};
    size_t instanceBufferSize{128};

    uint32_t visibleMeshletUpperBound{0};

    // New meshlet instancing buffers
    size_t instanceMeshletOffsetsBufferSize{128};
    size_t level1SumsBufferSize{128};
    size_t level1BlockSumsBufferSize{128};
    size_t level2SumsBufferSize{128};
    size_t level2BlockSumsBufferSize{128};
    size_t scannedLevel2BlockSumsBufferSize{128};

    size_t intermediateMeshletBufferSize{128};
    size_t meshletLevel1SumsBufferSize{128};
    size_t meshletLevel1BlockSumsBufferSize{128};
    size_t meshletLevel2SumsBufferSize{128};
    size_t meshletLevel2BlockSumsBufferSize{128};
    size_t meshletScannedLevel2BlockSumsBufferSize{128};
    size_t visibleMeshletsBufferSize{128};       // Final compacted output{}

    size_t glyphQuadBufferSize{128};
    size_t uiGlyphQuadBufferSize{128};
    size_t textInstanceBufferSize{128};
    size_t textMaterialBufferSize{128};
};
} // Render

#endif //WILL_ENGINE_RENDER_TYPES_H
