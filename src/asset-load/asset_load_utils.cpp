//
// Created by William on 2026-04-06.
//

#include "asset_load_utils.h"

#include "asset-load/asset_load_types.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "engine/logging/engine_log.h"

namespace AssetLoad
{
static constexpr uint32_t kEmissiveTriExtractCap = 8192;

static bool IsEmissiveMaterial(const MaterialProperties& props)
{
    const float maxEmissive = glm::max(props.emissiveFactor.x, glm::max(props.emissiveFactor.y, props.emissiveFactor.z));
    return (props.emissiveFactor.w > 0.0f && maxEmissive > 0.0f) || props.textureImageIndices.w >= 0;
}

void ExtractEmissiveTriangles(const UnpackedStaticModel& rawData, Engine::StaticModel* outputModel, Core::MemoryManager* memoryManager, uint32_t primitiveOffsetCount, bool bForceAllEmissive)
{
    auto primitiveTriCount = [&](const Engine::PrimitiveProperty& prop) -> uint32_t {
        const uint32_t localPi = prop.index;
        const size_t indexStart = rawData.primitives[localPi].indexOffset;
        const size_t indexEnd = localPi + 1 < rawData.primitives.Size() ? rawData.primitives[localPi + 1].indexOffset : rawData.indices.Size();
        return static_cast<uint32_t>((indexEnd - indexStart) / 3);
    };
    auto qualifies = [&](const Engine::PrimitiveProperty& prop) {
        if (prop.index >= rawData.primitives.Size()) { return false; }
        if (!bForceAllEmissive) {
            if (prop.materialIndex < 0 || static_cast<size_t>(prop.materialIndex) >= rawData.materials.Size()) { return false; }
            if (!IsEmissiveMaterial(rawData.materials[prop.materialIndex].props)) { return false; }
        }
        return primitiveTriCount(prop) > 0;
    };

    uint32_t setCount = 0;
    for (const auto& mesh : rawData.allMeshes) {
        for (const auto& prop : mesh.primitiveProperties) {
            if (qualifies(prop)) { setCount++; }
        }
    }
    if (setCount == 0) { return; }

    Core::HeapArray<Engine::EmissiveTriangleSet>& dst = outputModel->modelData.emissiveTriangles;
    assert(!dst.IsAllocated() && "modelData.emissiveTriangles was found to be allocated (memory leak)");
    dst = Core::HeapArray<Engine::EmissiveTriangleSet>(&memoryManager->Assets(), Core::AllocTag::AssetModel, setCount);

    uint32_t setIndex = 0;
    for (const auto& mesh : rawData.allMeshes) {
        for (const auto& prop : mesh.primitiveProperties) {
            if (!qualifies(prop)) { continue; }

            const uint32_t localPi = prop.index;
            const Primitive& prim = rawData.primitives[localPi];
            const size_t indexStart = prim.indexOffset;
            const uint32_t triCount = primitiveTriCount(prop);
            const uint32_t extractCount = std::min(triCount, kEmissiveTriExtractCap);

            Engine::EmissiveTriangleSet& set = dst[setIndex++];
            set.primitiveIndex = prop.index + primitiveOffsetCount;
            set.bTruncated = extractCount < triCount;
            set.verts = Core::HeapArray<Vec3>(&memoryManager->Assets(), Core::AllocTag::AssetModel, static_cast<size_t>(extractCount) * 3);

            const Vec3 boundsMin = prim.boundingBoxMin;
            const Vec3 boundsExtents = prim.boundingBoxMax - prim.boundingBoxMin;
            for (uint32_t t = 0; t < extractCount; ++t) {
                Vec3 v[3];
                for (uint32_t c = 0; c < 3; ++c) {
                    const Engine::Vertex& vert = rawData.vertices[rawData.indices[indexStart + static_cast<size_t>(t) * 3 + c]];
                    v[c] = DequantizeVertexPosition(vert, boundsMin, boundsExtents);
                }
                set.verts[t * 3 + 0] = v[0];
                set.verts[t * 3 + 1] = v[1] - v[0];
                set.verts[t * 3 + 2] = v[2] - v[0];
            }
            if (set.bTruncated) {
                LOG_WARN(Asset, "[ExtractEmissiveTriangles] {}: emissive primitive {} truncated to {} of {} triangles (extraction cap)", outputModel->name.c_str(), prop.index, extractCount, triCount);
            }
        }
    }
}

Mat3 JacobiEigen3x3(const Mat3& symMat)
{
    float a[3][3];
    float v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            a[r][c] = symMat[c][r];

    for (int iter = 0; iter < 32; ++iter) {
        int p = 0, q = 1;
        float maxAbs = abs(a[0][1]);
        if (abs(a[0][2]) > maxAbs) {
            maxAbs = abs(a[0][2]);
            p = 0;
            q = 2;
        }
        if (abs(a[1][2]) > maxAbs) {
            maxAbs = abs(a[1][2]);
            p = 1;
            q = 2;
        }
        if (maxAbs < 1e-12f) break;

        const float apq = a[p][q];
        float theta = 0.5f * (a[q][q] - a[p][p]) / apq;
        float t = 1.f / (abs(theta) + sqrt(theta * theta + 1.f));
        if (theta < 0.f) t = -t;
        const float c = 1.f / sqrt(t * t + 1.f);
        const float s = t * c;

        a[p][p] -= t * apq;
        a[q][q] += t * apq;
        a[p][q] = a[q][p] = 0.f;

        for (int r = 0; r < 3; ++r) {
            if (r == p || r == q) continue;
            const float arp = a[r][p], arq = a[r][q];
            a[r][p] = a[p][r] = c * arp - s * arq;
            a[r][q] = a[q][r] = s * arp + c * arq;
        }
        for (int r = 0; r < 3; ++r) {
            const float vrp = v[r][p], vrq = v[r][q];
            v[r][p] = c * vrp - s * vrq;
            v[r][q] = s * vrp + c * vrq;
        }
    }

    Mat3 result;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            result[col][row] = v[row][col];
    return result;
}

Engine::MeshBounds CalculateMeshBounds(Core::Span<Engine::FullVertex> vertices)
{
    Engine::MeshBounds result{};

    for (const auto& v : vertices) {
        result.aabb.min = glm::min(result.aabb.min, v.position);
        result.aabb.max = glm::max(result.aabb.max, v.position);
    }

    Vec3 center = result.aabb.Center();
    float radiusSq = 0.f;
    for (const auto& v : vertices) {
        const Vec3 d = v.position - center;
        radiusSq = glm::max(radiusSq, glm::dot(d, d));
    }

    result.sphere.center = center;
    result.sphere.radius = glm::sqrt(radiusSq);
    result.aabbExtents = result.aabb.max - result.aabb.min;

    return result;
}

Engine::ModelBounds ComputeBounds(Core::Span<Vec3> positions)
{
    if (positions.IsEmpty()) return {};

    Engine::ModelBounds out{};

    const float invN = 1.f / static_cast<float>(positions.Size());

    Vec3 mn(FLT_MAX), mx(-FLT_MAX), centroidAccum(0.f);
    for (const auto& p : positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
        centroidAccum += p;
    }
    out.aabb.min = mn;
    out.aabb.max = mx;
    out.centroid = centroidAccum * invN;

    const Vec3 he = out.aabb.HalfExtents();
    out.dominantAxis = (he.x >= he.y && he.x >= he.z) ? 0 : (he.y >= he.z ? 1 : 2);

    const Vec3 center = (mn + mx) * 0.5f;
    float maxDist2 = 0.f;
    for (const auto& p : positions) {
        const Vec3 d = p - center;
        maxDist2 = std::max(maxDist2, glm::dot(d, d));
    }
    out.sphere.center = center;
    out.sphere.radius = sqrt(maxDist2);

    Mat3 cov(0.f);
    for (const auto& p : positions) {
        const Vec3 d = p - center;
        cov[0][0] += d.x * d.x;
        cov[1][1] += d.y * d.y;
        cov[2][2] += d.z * d.z;
        const float xy = d.x * d.y, xz = d.x * d.z, yz = d.y * d.z;
        cov[1][0] += xy;
        cov[0][1] += xy;
        cov[2][0] += xz;
        cov[0][2] += xz;
        cov[2][1] += yz;
        cov[1][2] += yz;
    }
    cov *= invN;

    const Mat3 axes = JacobiEigen3x3(cov);

    Vec3 minProj(FLT_MAX), maxProj(-FLT_MAX);
    for (const auto& p : positions) {
        const Vec3 d = p - center;
        const Vec3 proj(
            glm::dot(d, Vec3(axes[0])),
            glm::dot(d, Vec3(axes[1])),
            glm::dot(d, Vec3(axes[2])));
        minProj = glm::min(minProj, proj);
        maxProj = glm::max(maxProj, proj);
    }
    out.obb.halfExtents = (maxProj - minProj) * 0.5f;
    out.obb.center = center + axes * ((minProj + maxProj) * 0.5f);
    out.obb.orientation = glm::quat_cast(axes);

    return out;
}
} // AssetLoad
