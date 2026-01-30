//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_DEBUG_RENDERER_H
#define WILL_ENGINE_DEBUG_RENDERER_H

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRenderer.h>

namespace Core
{
struct ViewFamily;
}

namespace Physics
{
class DebugRenderer final : public JPH::DebugRenderer
{
public:
    DebugRenderer();
    ~DebugRenderer() override;

    void SetViewFamily(Core::ViewFamily* _viewFamily) { this->viewFamily = _viewFamily; }

    void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

    void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
                      JPH::ColorArg inColor, ECastShadow inCastShadow) override;

    Batch CreateTriangleBatch(const Triangle* inTriangles, int inTriangleCount) override;

    Batch CreateTriangleBatch(const Vertex* inVertices, int inVertexCount,
                             const JPH::uint32* inIndices, int inIndexCount) override;

    void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds,
                     float inLODScaleSq, JPH::ColorArg inModelColor, const GeometryRef& inGeometry,
                     ECullMode inCullMode, ECastShadow inCastShadow, EDrawMode inDrawMode) override;

    void DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view& inString,
                   JPH::ColorArg inColor, float inHeight) override;

    class BatchImpl final : public JPH::RefTargetVirtual
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        void AddRef() override { ++refCount; }
        void Release() override { if (--refCount == 0) delete this; }

        JPH::Array<Triangle> triangles;

    private:
        std::atomic<uint32_t> refCount = 0;
    };

private:
    Core::ViewFamily* viewFamily = nullptr;
    JPH::RVec3 cameraPos;
    bool cameraPosSet = false;
};
} // Physics

#endif // JPH_DEBUG_RENDERER

#endif // WILL_ENGINE_DEBUG_RENDERER_H