//
// Created by William on 2026-04-06.
//

#include "asset_load_utils.h"

namespace AssetLoad
{
static glm::mat3 JacobiEigen3x3(const glm::mat3& symMat)
{
    float a[3][3];
    float v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            a[r][c] = symMat[c][r];

    for (int iter = 0; iter < 32; ++iter) {
        int p = 0, q = 1;
        float maxAbs = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > maxAbs) {
            maxAbs = std::abs(a[0][2]);
            p = 0;
            q = 2;
        }
        if (std::abs(a[1][2]) > maxAbs) {
            maxAbs = std::abs(a[1][2]);
            p = 1;
            q = 2;
        }
        if (maxAbs < 1e-12f) break;

        const float apq = a[p][q];
        float theta = 0.5f * (a[q][q] - a[p][p]) / apq;
        float t = 1.f / (std::abs(theta) + std::sqrt(theta * theta + 1.f));
        if (theta < 0.f) t = -t;
        const float c = 1.f / std::sqrt(t * t + 1.f);
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

    glm::mat3 result;
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            result[col][row] = v[row][col];
    return result;
}

Engine::ModelBounds ComputeBounds(Core::Span<Vec3> positions, Core::Span<uint32_t> indices)
{
    if (positions.IsEmpty()) return {};

    Engine::ModelBounds out{};

    const float invN = 1.f / static_cast<float>(positions.Size());

    glm::vec3 mn(FLT_MAX), mx(-FLT_MAX), centroidAccum(0.f);
    for (const auto& p : positions) {
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
        centroidAccum += p;
    }
    out.aabb.min = mn;
    out.aabb.max = mx;
    out.centroid = centroidAccum * invN;

    const glm::vec3 he = out.aabb.HalfExtents();
    out.dominantAxis = (he.x >= he.y && he.x >= he.z) ? 0 : (he.y >= he.z ? 1 : 2);

    const glm::vec3 center = (mn + mx) * 0.5f;
    float maxDist2 = 0.f;
    for (const auto& p : positions) {
        const glm::vec3 d = p - center;
        maxDist2 = std::max(maxDist2, glm::dot(d, d));
    }
    out.sphere.center = center;
    out.sphere.radius = std::sqrt(maxDist2);

    glm::mat3 cov(0.f);
    for (const auto& p : positions) {
        const glm::vec3 d = p - center;
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

    const glm::mat3 axes = JacobiEigen3x3(cov);

    glm::vec3 minProj(FLT_MAX), maxProj(-FLT_MAX);
    for (const auto& p : positions) {
        const glm::vec3 d = p - center;
        const glm::vec3 proj(
            glm::dot(d, glm::vec3(axes[0])),
            glm::dot(d, glm::vec3(axes[1])),
            glm::dot(d, glm::vec3(axes[2])));
        minProj = glm::min(minProj, proj);
        maxProj = glm::max(maxProj, proj);
    }
    out.obb.halfExtents = (maxProj - minProj) * 0.5f;
    out.obb.center = center + axes * ((minProj + maxProj) * 0.5f);
    out.obb.orientation = glm::quat_cast(axes);

    if (!indices.IsEmpty() && indices.Size() >= 3) {
        for (size_t i = 0; i + 2 < indices.Size(); i += 3) {
            const glm::vec3& a = positions[indices[i]];
            const glm::vec3& b = positions[(indices)[i + 1]];
            const glm::vec3& c = positions[(indices)[i + 2]];
            out.surfaceArea += glm::length(glm::cross(b - a, c - a)) * 0.5f;
        }
    }

    return out;
}
} // AssetLoad
