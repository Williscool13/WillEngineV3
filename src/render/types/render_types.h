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
#include "render/shaders/common_interop.h"

namespace Render
{
Frustum CreateFrustum(const glm::mat4& viewProj);

bool IntersectsSphere(const Frustum& frustum, const glm::vec3& center, float radius);

bool IntersectsAABB(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max);

bool IntersectsOBB(const Frustum& frustum, const glm::vec3& center, const glm::vec3& extents, const glm::mat3& rotation);

int32_t GetSphereSegments(const glm::vec3& center, const glm::vec3& viewPos, float radius);

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

    size_t modelBufferSize{128};
    size_t materialBufferSize{128};
    size_t shadeDispatchBufferSize{128};
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
    size_t visibleMeshletsBufferSize{128};       // Final compacted output

    void Reset()
    {
        viewFamily = nullptr;
        bCanRender = false;
        bWireframe = false;

        modelBufferSize = 128;
        materialBufferSize = 128;
        shadeDispatchBufferSize = 128;
        instanceBufferSize = 128;
    }
};
} // Render

#endif //WILL_ENGINE_RENDER_TYPES_H
