//
// Created by William on 2026-04-19.
//

#include "render_utils.h"

#include <meshoptimizer.h>


Vec2 OctEncode(Vec3 n)
{
    n /= glm::abs(n.x) + glm::abs(n.y) + glm::abs(n.z);
    if (n.z < 0.0f) {
        float x = n.x;
        n.x = (1.0f - glm::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
        n.y = (1.0f - glm::abs(x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    }
    return glm::vec2(n.x, n.y);
}

Vec3 OctDecode(Vec2 f)
{
    glm::vec3 n = glm::vec3(f.x, f.y, 1.0f - glm::abs(f.x) - glm::abs(f.y));
    float t = glm::clamp(-n.z, 0.0f, 1.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return glm::normalize(n);
}

Engine::Vertex CompressVertex(const Engine::FullVertex& fullVertex, const Engine::MeshBounds& bounds)
{
    Engine::Vertex outVertex{};
    uint32_t px = static_cast<uint32_t>(meshopt_quantizeUnorm((fullVertex.position.x - bounds.aabb.min.x) / bounds.aabbExtents.x, 16));
    uint32_t py = static_cast<uint32_t>(meshopt_quantizeUnorm((fullVertex.position.y - bounds.aabb.min.y) / bounds.aabbExtents.y, 16));
    uint32_t pz = static_cast<uint32_t>(meshopt_quantizeUnorm((fullVertex.position.z - bounds.aabb.min.z) / bounds.aabbExtents.z, 16));
    outVertex.pos0 = px | (py << 16);
    outVertex.pos1 = pz;

    Vec2 octN = OctEncode(glm::normalize(fullVertex.normal));
    uint32_t nx = static_cast<uint8_t>(static_cast<int8_t>(meshopt_quantizeSnorm(octN.x, 8)));
    uint32_t ny = static_cast<uint8_t>(static_cast<int8_t>(meshopt_quantizeSnorm(octN.y, 8)));
    outVertex.normalOct = nx | (ny << 8);

    Vec2 octT = OctEncode(Vec3(fullVertex.tangent));
    uint32_t tx = static_cast<uint8_t>(static_cast<int8_t>(meshopt_quantizeSnorm(octT.x, 8)));
    uint32_t ty = static_cast<uint8_t>(static_cast<int8_t>(meshopt_quantizeSnorm(octT.y, 8)));
    uint32_t ts = fullVertex.tangent.w > 0.f ? 1u : 0u;
    outVertex.tangentOct = tx | (ty << 8) | (ts << 16);

    uint32_t tu = meshopt_quantizeHalf(fullVertex.uv.x);
    uint32_t tv = meshopt_quantizeHalf(fullVertex.uv.y);
    outVertex.texcoord = tu | (tv << 16);

    uint32_t cr = static_cast<uint32_t>(glm::clamp(fullVertex.color.x, 0.f, 1.f) * 255.f);
    uint32_t cg = static_cast<uint32_t>(glm::clamp(fullVertex.color.y, 0.f, 1.f) * 255.f);
    uint32_t cb = static_cast<uint32_t>(glm::clamp(fullVertex.color.z, 0.f, 1.f) * 255.f);
    uint32_t ca = static_cast<uint32_t>(glm::clamp(fullVertex.color.w, 0.f, 1.f) * 255.f);
    outVertex.color = cr | (cg << 8) | (cb << 16) | (ca << 24);

    return outVertex;
}

struct CompressVerticesTask final : enki::ITaskSet
{
    const Engine::FullVertex* src{};
    const Engine::MeshBounds* bounds{};
    Engine::Vertex* dst{};

    void ExecuteRange(enki::TaskSetPartition range, uint32_t) override
    {
        for (uint32_t i = range.start; i < range.end; ++i) {
            dst[i] = CompressVertex(src[i], *bounds);
        }
    }
};

void CompressVertices(enki::TaskScheduler* scheduler, const Engine::FullVertex* src, uint32_t count, const Engine::MeshBounds& bounds, Engine::Vertex* dst)
{
    constexpr uint32_t PARALLEL_THRESHOLD = 8192;
    if (!scheduler || count < PARALLEL_THRESHOLD) {
        for (uint32_t i = 0; i < count; ++i) {
            dst[i] = CompressVertex(src[i], bounds);
        }
        return;
    }

    CompressVerticesTask task;
    task.m_SetSize = count;
    task.m_MinRange = 4096;
    task.src = src;
    task.bounds = &bounds;
    task.dst = dst;
    scheduler->AddTaskSetToPipe(&task);
    scheduler->WaitforTask(&task);
}
