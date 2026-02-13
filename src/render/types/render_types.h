//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_RENDER_TYPES_H
#define WILL_ENGINE_RENDER_TYPES_H
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/include/render_interface.h"
#include "render/shaders/common_interop.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj);

bool IntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius);

bool IntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max);

bool IntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation);

int32_t GetSphereSegments(const glm::vec3& center, const glm::vec3& viewPos, float radius);

struct RenderFamilyProperties
{
    Core::ViewFamily* viewFamily{nullptr};
    bool bHasMainGeometry{false};
    bool bHasDirectGeometry{false};
    bool bHasAnyGeometry{false};
    bool bHasGTAO{false};
    bool bHasShadows{false};
    bool bHasDeferred{false};

    size_t modelBufferSize{128};
    size_t materialBufferSize{128};
    size_t instanceBufferSize{128};

    size_t primitiveIndexToPrimitiveCounterBufferSize{MEGA_PRIMITIVE_BUFFER_COUNT * sizeof(uint32_t)};

    size_t instanceIndirectionBufferSize{128};
    size_t primitivePrefixSumBufferSize{128};
    size_t primitivePrefixBlockSumBufferSize{128};
    size_t primitiveCountersBufferSize{128};
    size_t mainCommandBufferSize{128};

    size_t directInstanceBufferSize{128};
    size_t directIndirectCommandBufferSize{128};

    uint32_t instanceCount{0};
    uint32_t filteredPrimitiveCount{0};

    std::vector<uint32_t> primitiveIndexToRangeBufferMap{};

    // New meshlet instancing buffers
    size_t instanceMeshletOffsetsBufferSize{128};
    size_t level1SumsBufferSize{128};
    size_t level1BlockSumsBufferSize{128};
    size_t level2SumsBufferSize{128};
    size_t level2BlockSumsBufferSize{128};
    size_t scannedLevel2BlockSumsBufferSize{128};
    size_t intermediateMeshletBufferSize{MAX_MESHLET_COUNT_PER_FRAME * sizeof(uint32_t)};
    size_t visibleMeshletsBufferSize{128};       // Final compacted output

    void Reset()
    {
        viewFamily = nullptr;
        bHasMainGeometry = false;
        bHasDirectGeometry = false;
        bHasAnyGeometry = false;
        bHasGTAO = false;
        bHasShadows = false;
        bHasDeferred = false;

        modelBufferSize = 128;
        materialBufferSize = 128;
        instanceBufferSize = 128;

        primitiveIndexToPrimitiveCounterBufferSize = MEGA_PRIMITIVE_BUFFER_COUNT * sizeof(uint32_t);

        instanceIndirectionBufferSize = 128;
        primitivePrefixSumBufferSize = 128;
        primitivePrefixBlockSumBufferSize = 128;
        primitiveCountersBufferSize = 128;
        mainCommandBufferSize = 128;

        directInstanceBufferSize = 128;
        directIndirectCommandBufferSize = 128;

        instanceCount = 0;
        filteredPrimitiveCount = 0;

        primitiveIndexToRangeBufferMap.clear();
    }
};
} // Render

#endif //WILL_ENGINE_RENDER_TYPES_H
