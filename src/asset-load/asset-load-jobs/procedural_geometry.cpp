//
// Created by William on 2026-07-03.
//

#include "procedural_geometry.h"

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "core/containers/heap_array.h"
#include "par/par_shapes.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
bool GenerateKleinBottleGeometry(const Engine::KleinBottleParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    ZoneScopedN("GenerateKleinBottleGeometry");

    outVertices.Clear();
    outIndices.Clear();

    par_shapes_mesh* m = par_shapes_create_klein_bottle(std::max(3, p.slices), std::max(3, p.stacks));
    if (!m) { return false; }

    for (int i = 0; i < m->npoints; ++i) {
        Engine::FullVertex v{};
        v.position = Vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        v.normal = m->normals ? glm::normalize(Vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : Vec3{0, 1, 0};
        v.uv = {m->tcoords ? m->tcoords[i * 2] : 0.0f, m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        outVertices.PushBack(v);
    }
    for (int i = 0; i < m->ntriangles * 3; ++i) { outIndices.PushBack(static_cast<uint32_t>(m->triangles[i])); }
    par_shapes_free_mesh(m);

    return !outVertices.IsEmpty() && !outIndices.IsEmpty();
}

bool GenerateTrefoilKnotGeometry(const Engine::TrefoilKnotParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    ZoneScopedN("GenerateTrefoilKnotGeometry");

    outVertices.Clear();
    outIndices.Clear();

    const float tubeRadius = glm::clamp(p.tubeRadius, 0.5f, 3.0f);
    par_shapes_mesh* m = par_shapes_create_trefoil_knot(std::max(3, p.slices), std::max(3, p.stacks), tubeRadius);
    if (!m) { return false; }

    for (int i = 0; i < m->npoints; ++i) {
        Engine::FullVertex v{};
        v.position = Vec3{m->points[i * 3], m->points[i * 3 + 1], m->points[i * 3 + 2]} * p.scale;
        v.normal = m->normals ? glm::normalize(Vec3{m->normals[i * 3], m->normals[i * 3 + 1], m->normals[i * 3 + 2]}) : Vec3{0, 1, 0};
        v.uv = {m->tcoords ? m->tcoords[i * 2] : 0.0f, m->tcoords ? m->tcoords[i * 2 + 1] : 0.0f};
        v.tangent = {1, 0, 0, 1};
        v.color = {1, 1, 1, 1};
        outVertices.PushBack(v);
    }
    for (int i = 0; i < m->ntriangles * 3; ++i) { outIndices.PushBack(static_cast<uint32_t>(m->triangles[i])); }
    par_shapes_free_mesh(m);

    return !outVertices.IsEmpty() && !outIndices.IsEmpty();
}

bool GenerateCurvedRampGeometry(const Engine::CurvedRampParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    ZoneScopedN("GenerateCurvedRampGeometry");

    outVertices.Clear();
    outIndices.Clear();

    const float w = std::max(0.01f, p.width);
    const float h = std::max(0.01f, p.height);
    const float R = glm::clamp(p.radius, 0.01f, h);
    const int seg = std::max(2, p.segments);
    const float pi = glm::pi<float>();
    const float hw = w * 0.5f;
    const float fl = p.bHalfPipe ? std::max(0.0f, p.flatLength) : 0.0f;
    const float lip = std::max(0.0f, p.lipHeight);

    // Profile in YZ plane. Normals point toward the concave (rider) side.
    // Curve 1 center: (Z=R, Y=R). Angle PI (top) to 3PI/2 (bottom).
    // Curve 2 center: (Z=R+fl, Y=R). Angle -PI/2 (bottom) to 0 (top).
    const int profCapacity = (h > R ? 2 : 0) + (seg + 1)
        + (p.bHalfPipe ? (fl > 0.0f ? 1 : 0) + (seg + 1) + (h > R ? 2 : 0) : 0);
    Core::HeapArray<Vec3> profile(&scratch, Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<Vec3> profileN(&scratch, Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    int pi_ = 0;

    if (h > R) {
        profile[pi_] = {0.0f, lip + h, 0.0f};
        profileN[pi_] = {0.0f, 0.0f, 1.0f};
        pi_++;
        profile[pi_] = {0.0f, lip + R, 0.0f};
        profileN[pi_] = {0.0f, 0.0f, 1.0f};
        pi_++;
    }

    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = pi + t * (pi * 0.5f);
        const float z = R + R * cosf(angle);
        const float y = lip + R + R * sinf(angle);
        profile[pi_] = {0.0f, y, z};
        profileN[pi_] = glm::normalize(Vec3{0.0f, -sinf(angle), -cosf(angle)});
        pi_++;
    }

    if (p.bHalfPipe) {
        if (fl > 0.0f) {
            profile[pi_] = {0.0f, lip, R + fl};
            profileN[pi_] = {0.0f, 1.0f, 0.0f};
            pi_++;
        }

        const float zCenter2 = R + fl;
        for (int i = 0; i <= seg; i++) {
            const float t = static_cast<float>(i) / seg;
            const float angle = -pi * 0.5f + t * (pi * 0.5f);
            const float z = zCenter2 + R * cosf(angle);
            const float y = lip + R + R * sinf(angle);
            profile[pi_] = {0.0f, y, z};
            profileN[pi_] = glm::normalize(Vec3{0.0f, -sinf(angle), -cosf(angle)});
            pi_++;
        }

        if (h > R) {
            const float zBack = 2.0f * R + fl;
            profile[pi_] = {0.0f, lip + R, zBack};
            profileN[pi_] = {0.0f, 0.0f, -1.0f};
            pi_++;
            profile[pi_] = {0.0f, lip + h, zBack};
            profileN[pi_] = {0.0f, 0.0f, -1.0f};
            pi_++;
        }
    }

    const int profCount = profCapacity;
    const float totalDepth = p.bHalfPipe ? 2.0f * R + fl : R;

    auto pushVert = [&](Vec3 pos, Vec3 n, float u, float v) {
        Engine::FullVertex vtx{};
        vtx.position = pos;
        vtx.normal = n;
        vtx.uv = {u, v};
        vtx.tangent = {1, 0, 0, 1};
        vtx.color = {1, 1, 1, 1};
        outVertices.PushBack(vtx);
    };

    // Riding surface: extrude profile along X
    uint32_t surfBase = static_cast<uint32_t>(outVertices.Size());
    for (int side = 0; side < 2; side++) {
        const float x = side == 0 ? -hw : hw;
        const float u = side == 0 ? 0.0f : 1.0f;
        for (int i = 0; i < profCount; i++) {
            const float v = static_cast<float>(i) / (profCount - 1);
            pushVert({x, profile[i].y, profile[i].z}, profileN[i], u, v);
        }
    }
    for (int i = 0; i < profCount - 1; i++) {
        uint32_t a0 = surfBase + i;
        uint32_t a1 = surfBase + i + 1;
        uint32_t b0 = surfBase + profCount + i;
        uint32_t b1 = surfBase + profCount + i + 1;
        const uint32_t tri[6] = {a0, a1, b0, b0, a1, b1};
        outIndices.Append(tri, tri + 6);
    }

    // Back wall at Z=0
    {
        uint32_t base = static_cast<uint32_t>(outVertices.Size());
        Vec3 n = {0, 0, -1};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, lip + h, 0}, n, 1, 1);
        pushVert({-hw, lip + h, 0}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        outIndices.Append(tri, tri + 6);
    }

    // Far wall (half-pipe only)
    if (p.bHalfPipe) {
        uint32_t base = static_cast<uint32_t>(outVertices.Size());
        Vec3 n = {0, 0, 1};
        pushVert({-hw, 0, totalDepth}, n, 0, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 0);
        pushVert({hw, lip + h, totalDepth}, n, 1, 1);
        pushVert({-hw, lip + h, totalDepth}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        outIndices.Append(tri, tri + 6);
    }

    // Bottom
    {
        uint32_t base = static_cast<uint32_t>(outVertices.Size());
        Vec3 n = {0, -1, 0};
        pushVert({-hw, 0, 0}, n, 0, 0);
        pushVert({hw, 0, 0}, n, 1, 0);
        pushVert({hw, 0, totalDepth}, n, 1, 1);
        pushVert({-hw, 0, totalDepth}, n, 0, 1);
        const uint32_t tri[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        outIndices.Append(tri, tri + 6);
    }

    // Side walls
    auto emitFan = [&](uint32_t base, int count, int side) {
        for (int i = 1; i < count - 1; i++) {
            if (side == 0) {
                const uint32_t tri[3] = {base, base + (uint32_t)(i + 1), base + (uint32_t)i};
                outIndices.Append(tri, tri + 3);
            }
            else {
                const uint32_t tri[3] = {base, base + (uint32_t)i, base + (uint32_t)(i + 1)};
                outIndices.Append(tri, tri + 3);
            }
        }
    };

    auto sideUV = [&](int i) -> Vec2 {
        return {profile[i].z / std::max(totalDepth, 0.01f), profile[i].y / std::max(h, 0.01f)};
    };

    // Profile split index: end of curve 1 + flat section (where Y=0 before curve 2)
    const int splitIdx = (h > R ? 2 : 0) + (seg + 1) + (fl > 0.0f ? 1 : 0);

    for (int side = 0; side < 2; side++) {
        const float x = side == 0 ? -hw : hw;
        const Vec3 n = side == 0 ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};

        if (!p.bHalfPipe) {
            uint32_t base = static_cast<uint32_t>(outVertices.Size());
            pushVert({x, 0, 0}, n, 0, 0);
            pushVert({x, lip + h, 0}, n, 0, 1);
            for (int i = 0; i < profCount; i++) {
                auto uv = sideUV(i);
                pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
            }
            pushVert({x, 0, totalDepth}, n, 1, 0);
            emitFan(base, 2 + profCount + 1, side);
        }
        else {
            // Near fan: bottom-near corner, wall 1, curve 1, flat
            {
                uint32_t base = static_cast<uint32_t>(outVertices.Size());
                pushVert({x, 0, 0}, n, 0, 0);
                pushVert({x, lip + h, 0}, n, 0, 1);
                for (int i = 0; i < splitIdx; i++) {
                    auto uv = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
                }
                emitFan(base, 2 + splitIdx, side);
            }
            // Far fan: bottom-far corner, curve 2, wall 2
            {
                uint32_t base = static_cast<uint32_t>(outVertices.Size());
                pushVert({x, 0, totalDepth}, n, 1, 0);
                for (int i = splitIdx; i < profCount; i++) {
                    auto uv = sideUV(i);
                    pushVert({x, profile[i].y, profile[i].z}, n, uv.x, uv.y);
                }
                pushVert({x, lip + h, totalDepth}, n, 1, 1);
                emitFan(base, 1 + (profCount - splitIdx) + 1, side);
            }
        }
    }

    return !outVertices.IsEmpty() && !outIndices.IsEmpty();
}

bool GenerateBowlGeometry(const Engine::BowlParams& p, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    ZoneScopedN("GenerateBowlGeometry");

    outVertices.Clear();
    outIndices.Clear();

    const float outerR = std::max(0.01f, p.radius);
    const float h = std::max(0.01f, p.height);
    const float cR = glm::clamp(p.curveRadius, 0.01f, h);
    const float flatR = glm::clamp(p.flatRadius, 0.0f, outerR - cR);
    const float lip = std::max(0.0f, p.lipHeight);
    const int N = std::max(3, p.slices);
    const int seg = std::max(2, p.segments);
    const float pi = glm::pi<float>();

    // Profile in YR plane (R = radial distance from center, Y = height).
    // Revolved around Y axis. Normals point inward (toward bowl center).
    // Curve center: (R = flatR, Y = cR). Angle 0 (wall) to -PI/2 (floor).
    const int profCapacity = (h > cR ? 2 : 0) + (seg + 1) + (flatR > 0.0f ? 1 : 0);
    Core::HeapArray<float> profR(&scratch, Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<float> profY(&scratch, Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    Core::HeapArray<Vec2> profN(&scratch, Core::AllocTag::AssetModel, static_cast<size_t>(profCapacity));
    int pci = 0;

    // Vertical wall above the curve (if h > cR)
    if (h > cR) {
        profR[pci] = flatR + cR; profY[pci] = lip + h; profN[pci] = {-1.0f, 0.0f}; pci++;
        profR[pci] = flatR + cR; profY[pci] = lip + cR; profN[pci] = {-1.0f, 0.0f}; pci++;
    }

    // Quarter-circle from wall down to floor
    for (int i = 0; i <= seg; i++) {
        const float t = static_cast<float>(i) / seg;
        const float angle = -t * (pi * 0.5f); // 0 to -PI/2
        profR[pci] = flatR + cR * cosf(angle);
        profY[pci] = lip + cR + cR * sinf(angle);
        profN[pci] = {-cosf(angle), -sinf(angle)};
        pci++;
    }

    // Flat floor (if flatR > 0)
    if (flatR > 0.0f) {
        profR[pci] = 0.0f; profY[pci] = lip; profN[pci] = {0.0f, 1.0f}; pci++;
    }

    const int profCount = profCapacity;

    // Revolve profile around Y axis
    for (int j = 0; j <= N; j++) {
        const float theta = static_cast<float>(j) / N * 2.0f * pi;
        const float cx = cosf(theta), cz = sinf(theta);
        for (int i = 0; i < profCount; i++) {
            Engine::FullVertex v{};
            v.position = {cx * profR[i], profY[i], cz * profR[i]};
            v.normal = {cx * profN[i].x, profN[i].y, cz * profN[i].x};
            v.uv = {static_cast<float>(j) / N, static_cast<float>(i) / (profCount - 1)};
            v.tangent = {-cz, 0, cx, 1.0f};
            v.color = {1, 1, 1, 1};
            outVertices.PushBack(v);
        }
    }

    for (int j = 0; j < N; j++) {
        for (int i = 0; i < profCount - 1; i++) {
            uint32_t a0 = j * profCount + i;
            uint32_t a1 = j * profCount + i + 1;
            uint32_t b0 = (j + 1) * profCount + i;
            uint32_t b1 = (j + 1) * profCount + i + 1;
            const uint32_t tri[6] = {a0, a1, b0, b0, a1, b1};
            outIndices.Append(tri, tri + 6);
        }
    }

    // Bottom face at Y=0 (pivot point)
    {
        uint32_t base = static_cast<uint32_t>(outVertices.Size());
        Engine::FullVertex cv{};
        cv.position = {0, 0, 0};
        cv.normal = {0, -1, 0};
        cv.uv = {0.5f, 0.5f};
        cv.tangent = {1, 0, 0, 1};
        cv.color = {1, 1, 1, 1};
        outVertices.PushBack(cv);

        const float rimR = flatR + cR;
        for (int j = 0; j < N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            Engine::FullVertex v{};
            v.position = {cosf(theta) * rimR, 0.0f, sinf(theta) * rimR};
            v.normal = {0, -1, 0};
            v.uv = {cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f};
            v.tangent = {1, 0, 0, 1};
            v.color = {1, 1, 1, 1};
            outVertices.PushBack(v);
        }
        for (int j = 0; j < N; j++) {
            const uint32_t tri[3] = {base, base + 1 + (uint32_t)j, base + 1 + (uint32_t)((j + 1) % N)};
            outIndices.Append(tri, tri + 3);
        }
    }

    // Outer wall (vertical cylinder at R = flatR + cR, from Y=0 to Y=lip+h)
    {
        const float wallR = flatR + cR;
        uint32_t base = static_cast<uint32_t>(outVertices.Size());
        for (int j = 0; j <= N; j++) {
            const float theta = static_cast<float>(j) / N * 2.0f * pi;
            const float cx = cosf(theta), cz = sinf(theta);
            for (int k = 0; k < 2; k++) {
                Engine::FullVertex v{};
                v.position = {cx * wallR, k == 0 ? 0.0f : lip + h, cz * wallR};
                v.normal = {cx, 0, cz};
                v.uv = {static_cast<float>(j) / N, static_cast<float>(k)};
                v.tangent = {-cz, 0, cx, 1.0f};
                v.color = {1, 1, 1, 1};
                outVertices.PushBack(v);
            }
        }
        for (int j = 0; j < N; j++) {
            uint32_t a0 = base + j * 2, a1 = base + j * 2 + 1;
            uint32_t b0 = base + (j + 1) * 2, b1 = base + (j + 1) * 2 + 1;
            const uint32_t tri[6] = {a0, a1, b0, a1, b1, b0};
            outIndices.Append(tri, tri + 6);
        }
    }

    return !outVertices.IsEmpty() && !outIndices.IsEmpty();
}

bool GenerateNonAnalyticProceduralGeometry(const Engine::ProceduralParams& params, Core::TlsfAllocator& scratch, Core::Vector<Engine::FullVertex>& outVertices, Core::Vector<uint32_t>& outIndices)
{
    if (const auto* p = std::get_if<Engine::KleinBottleParams>(&params)) { return GenerateKleinBottleGeometry(*p, scratch, outVertices, outIndices); }
    if (const auto* p = std::get_if<Engine::TrefoilKnotParams>(&params)) { return GenerateTrefoilKnotGeometry(*p, scratch, outVertices, outIndices); }
    if (const auto* p = std::get_if<Engine::CurvedRampParams>(&params)) { return GenerateCurvedRampGeometry(*p, scratch, outVertices, outIndices); }
    if (const auto* p = std::get_if<Engine::BowlParams>(&params)) { return GenerateBowlGeometry(*p, scratch, outVertices, outIndices); }
    return false;
}
} // AssetLoad
