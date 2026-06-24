//
// Created by William on 2026-06-22.
//

#include "light_bvh.h"

#include <algorithm>
#include <glm/glm.hpp>

#include "core/math/math_helpers.h"

namespace Render
{
static_assert(sizeof(LightBVHNode) == 32, "LightBVHNode must be 32 bytes; check glm::vec3 alignment");

/** Spread the low 10 bits of v so each occupies every third bit, for a 30-bit 3D Morton code. */
static uint32_t MortonExpandBits(uint32_t v)
{
    v = (v | (v << 16)) & 0x030000FFu;
    v = (v | (v << 8)) & 0x0300F00Fu;
    v = (v | (v << 4)) & 0x030C30C3u;
    v = (v | (v << 2)) & 0x09249249u;
    return v;
}

/** Morton code of a point in [0,1]^3 (10 bits per axis). */
static uint32_t Morton3D(glm::vec3 p)
{
    const float x = glm::clamp(p.x * 1024.0f, 0.0f, 1023.0f);
    const float y = glm::clamp(p.y * 1024.0f, 0.0f, 1023.0f);
    const float z = glm::clamp(p.z * 1024.0f, 0.0f, 1023.0f);
    return (MortonExpandBits(static_cast<uint32_t>(x)) << 2) | (MortonExpandBits(static_cast<uint32_t>(y)) << 1) |
           MortonExpandBits(static_cast<uint32_t>(z));
}

static float LightColorMax(uint32_t packedColor)
{
    const float r = static_cast<float>(packedColor & 0xFFu) / 255.0f;
    const float g = static_cast<float>((packedColor >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((packedColor >> 16) & 0xFFu) / 255.0f;
    return glm::max(r, glm::max(g, b));
}

uint32_t BuildLightBVH(const LightInfo* lights, uint32_t count, LightBVHNode* outNodes, uint32_t* outLightLeaf)
{
    if (count == 0u) { return 0u; }

    constexpr float PI = 3.14159265358979323846f;
    const uint32_t numLeaves = static_cast<uint32_t>(Core::Math::NextPowerOfTwo(count));
    const uint32_t leafBase = numLeaves - 1u;

    struct LightAttr
    {
        glm::vec3 centroid;
        glm::vec3 bmin;
        glm::vec3 bmax;
        float power;
        uint32_t idx;
        uint32_t morton;
    };
    static thread_local LightAttr attrs[MAX_LIGHTS];
    static thread_local uint32_t order[MAX_LIGHTS];

    glm::vec3 sceneMin(1e30f);
    glm::vec3 sceneMax(-1e30f);
    for (uint32_t i = 0; i < count; i++) {
        const LightInfo& L = lights[i];
        const glm::vec3 c = glm::vec3(L.position);
        glm::vec3 bmin;
        glm::vec3 bmax;
        float area;
        if (L.type == LIGHT_TYPE_SPHERE) {
            const float radius = L.right.w;
            bmin = c - glm::vec3(radius);
            bmax = c + glm::vec3(radius);
            area = 4.0f * PI * radius * radius;
        } else {
            const glm::vec3 ext = glm::abs(glm::vec3(L.right)) * L.right.w + glm::abs(glm::vec3(L.up)) * L.up.w;
            bmin = c - ext;
            bmax = c + ext;
            area = 4.0f * L.right.w * L.up.w;
        }
        attrs[i] = {c, bmin, bmax, L.intensity * area * LightColorMax(L.packedColor), i, 0u};
        order[i] = i;
        sceneMin = glm::min(sceneMin, c);
        sceneMax = glm::max(sceneMax, c);
    }

    const glm::vec3 invExtent = 1.0f / glm::max(sceneMax - sceneMin, glm::vec3(1e-6f));
    for (uint32_t i = 0; i < count; i++) {
        attrs[i].morton = Morton3D((attrs[i].centroid - sceneMin) * invExtent);
    }
    // Stable order, tiebreak by light index, so the leaf layout (and the inverse map) is deterministic given the same lights.
    std::sort(order, order + count, [&](uint32_t a, uint32_t b) {
        return attrs[a].morton != attrs[b].morton ? attrs[a].morton < attrs[b].morton : a < b;
    });

    for (uint32_t s = 0; s < numLeaves; s++) {
        LightBVHNode& node = outNodes[leafBase + s];
        if (s < count) {
            const LightAttr& a = attrs[order[s]];
            node.bmin = a.bmin;
            node.bmax = a.bmax;
            node.power = a.power;
            node.lightIdx = a.idx;
            outLightLeaf[a.idx] = s;
        } else {
            // Padding leaf: empty box (identity under min/max) and zero power so it is never selected.
            node.bmin = glm::vec3(1e30f);
            node.bmax = glm::vec3(-1e30f);
            node.power = 0.0f;
            node.lightIdx = ~0u;
        }
    }

    for (int i = static_cast<int>(leafBase) - 1; i >= 0; i--) {
        const LightBVHNode& l = outNodes[2 * i + 1];
        const LightBVHNode& r = outNodes[2 * i + 2];
        LightBVHNode& node = outNodes[i];
        node.bmin = glm::min(l.bmin, r.bmin);
        node.bmax = glm::max(l.bmax, r.bmax);
        node.power = l.power + r.power;
        node.lightIdx = ~0u;
    }

    return numLeaves;
}
} // namespace Render
