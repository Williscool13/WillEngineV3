//
// Created by William on 2026-01-30.
//

#ifdef JPH_DEBUG_RENDERER

#include "physics_debug_renderer.h"

#include "physics_helpers.h"
#include "core/include/render_interface.h"

namespace Physics
{
DebugRenderer::DebugRenderer()
{
    Initialize();
}

DebugRenderer::~DebugRenderer() = default;

void DebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, const JPH::ColorArg inColor)
{
    if (!viewFamily) return;

    viewFamily->debugLines.push_back({
        .start = PhysicsHelpers::ToGLM(inFrom),
        .end = PhysicsHelpers::ToGLM(inTo),
        .color = PhysicsHelpers::ToGLM(inColor)
    });
}

void DebugRenderer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                                 JPH::ColorArg inColor, ECastShadow inCastShadow)
{
    if (!viewFamily) return;

    glm::vec3 v1 = PhysicsHelpers::ToGLM(inV1);
    glm::vec3 v2 = PhysicsHelpers::ToGLM(inV2);
    glm::vec3 v3 = PhysicsHelpers::ToGLM(inV3);
    glm::vec4 color = PhysicsHelpers::ToGLM(inColor);

    viewFamily->debugLines.push_back({.start = v1, .end = v2, .color = color});
    viewFamily->debugLines.push_back({.start = v2, .end = v3, .color = color});
    viewFamily->debugLines.push_back({.start = v3, .end = v1, .color = color});
}

JPH::DebugRenderer::Batch DebugRenderer::CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount)
{
    const auto batch = new BatchImpl;
    if (inTriangles == nullptr || inTriangleCount == 0)
        return batch;

    batch->triangles.assign(inTriangles, inTriangles + inTriangleCount);
    return batch;
}

JPH::DebugRenderer::Batch DebugRenderer::CreateTriangleBatch(const Vertex* inVertices, int inVertexCount,
                                                             const JPH::uint32* inIndices, int inIndexCount)
{
    const auto batch = new BatchImpl;
    if (inVertices == nullptr || inVertexCount == 0 || inIndices == nullptr || inIndexCount == 0)
        return batch;

    // Convert indexed triangle list to triangle list
    batch->triangles.resize(inIndexCount / 3);
    for (size_t t = 0; t < batch->triangles.size(); ++t) {
        Triangle& triangle = batch->triangles[t];
        triangle.mV[0] = inVertices[inIndices[t * 3 + 0]];
        triangle.mV[1] = inVertices[inIndices[t * 3 + 1]];
        triangle.mV[2] = inVertices[inIndices[t * 3 + 2]];
    }

    return batch;
}

void DebugRenderer::DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds,
                                 float inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry,
                                 ECullMode inCullMode, ECastShadow inCastShadow, EDrawMode inDrawMode)
{
    // Figure out which LOD to use (default to LOD 0)
    const LOD* lod = inGeometry->mLODs.data();
    if (cameraPosSet)
        lod = &inGeometry->GetLOD(JPH::Vec3(cameraPos), inWorldSpaceBounds, inLODScaleSq);

    // Draw the batch
    const BatchImpl* batch = static_cast<const BatchImpl*>(lod->mTriangleBatch.GetPtr());
    for (const Triangle& triangle : batch->triangles) {
        const JPH::RVec3 v0 = inModelMatrix * JPH::Vec3(triangle.mV[0].mPosition);
        const JPH::RVec3 v1 = inModelMatrix * JPH::Vec3(triangle.mV[1].mPosition);
        const JPH::RVec3 v2 = inModelMatrix * JPH::Vec3(triangle.mV[2].mPosition);
        const JPH::Color color = inModelColor * triangle.mV[0].mColor;

        switch (inDrawMode) {
            case EDrawMode::Wireframe:
                DrawLine(v0, v1, color);
                DrawLine(v1, v2, color);
                DrawLine(v2, v0, color);
                break;

            case EDrawMode::Solid:
                DrawTriangle(v0, v1, v2, color, inCastShadow);
                break;
        }
    }
}

void DebugRenderer::DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view& inString,
                               JPH::ColorArg inColor, float inHeight)
{
    // Do not implement this
}
} // Physics

#endif // JPH_DEBUG_RENDERER
