//
// Created by William on 2026-06-22.
//

#include "light_bvh.h"

#include <algorithm>
#include <glm/glm.hpp>

#include "core/math/math_helpers.h"

namespace Render
{
static_assert(sizeof(LightBVHNode) == 52, "LightBVHNode must be 52 bytes; check glm::vec3 alignment");

static constexpr float LIGHT_BVH_PI = 3.14159265358979323846f;

struct LightCone
{
    glm::vec3 axis;
    float thetaO;
    float thetaE;
};

/**
 * Bounding cone of two orientation cones (Conty-Estevez & Kulla 2018). thetaE is the max of the two; the axis is
 * rotated from the wider cone toward the other by half the angular slack so the result encloses both.
 */
static LightCone ConeUnion(LightCone a, LightCone b)
{
    if (b.thetaO > a.thetaO) { std::swap(a, b); }
    const float thetaE = glm::max(a.thetaE, b.thetaE);
    const float thetaD = glm::acos(glm::clamp(glm::dot(a.axis, b.axis), -1.0f, 1.0f));
    if (glm::min(thetaD + b.thetaO, LIGHT_BVH_PI) <= a.thetaO) {
        return {a.axis, a.thetaO, thetaE};
    }
    const float thetaO = (a.thetaO + thetaD + b.thetaO) * 0.5f;
    if (thetaO >= LIGHT_BVH_PI) {
        return {a.axis, LIGHT_BVH_PI, thetaE};
    }
    const float thetaR = thetaO - a.thetaO;
    glm::vec3 rotAxis = glm::cross(a.axis, b.axis);
    const float len = glm::length(rotAxis);
    if (len < 1e-6f) {
        return {a.axis, thetaO, thetaE};
    }
    rotAxis /= len;
    const float c = glm::cos(thetaR);
    const float s = glm::sin(thetaR);
    const glm::vec3 axis = glm::normalize(a.axis * c + glm::cross(rotAxis, a.axis) * s + rotAxis * glm::dot(rotAxis, a.axis) * (1.0f - c));
    return {axis, thetaO, thetaE};
}

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
        LightCone cone;
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
        LightCone cone;
        if (L.type == LIGHT_TYPE_SPHERE) {
            const float radius = L.right.w;
            bmin = c - glm::vec3(radius);
            bmax = c + glm::vec3(radius);
            area = 4.0f * PI * radius * radius;
            // Isotropic emission: a full orientation cone (thetaO = pi) so it is never culled by the cone test.
            cone = {glm::vec3(0.0f, 0.0f, 1.0f), PI, PI * 0.5f};
        }
        else {
            const glm::vec3 ext = glm::abs(glm::vec3(L.right)) * L.right.w + glm::abs(glm::vec3(L.up)) * L.up.w;
            bmin = c - ext;
            bmax = c + ext;
            area = 4.0f * L.right.w * L.up.w;
            // One-sided front-hemisphere emission about the light normal (matches the cosFacing/cosLight one-sidedness).
            const glm::vec3 nrm = glm::vec3(L.normal);
            const float nl = glm::length(nrm);
            cone = {(nl > 1e-6f) ? (nrm / nl) : glm::vec3(0.0f, 0.0f, 1.0f), 0.0f, PI * 0.5f};
        }
        attrs[i] = {c, bmin, bmax, L.intensity * area * LightColorMax(L.packedColor), i, 0u, cone};
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
            node.coneAxis = a.cone.axis;
            node.coneThetaO = a.cone.thetaO;
            node.coneThetaE = a.cone.thetaE;
            outLightLeaf[a.idx] = s;
        }
        else {
            // Padding leaf: empty box (identity under min/max) and zero power so it is never selected; inert cone.
            node.bmin = glm::vec3(1e30f);
            node.bmax = glm::vec3(-1e30f);
            node.power = 0.0f;
            node.lightIdx = ~0u;
            node.coneAxis = glm::vec3(0.0f, 0.0f, 1.0f);
            node.coneThetaO = 0.0f;
            node.coneThetaE = 0.0f;
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
        // Union the children's orientation cones, skipping zero-power (padding/empty) subtrees so they never widen it.
        const LightCone lc{l.coneAxis, l.coneThetaO, l.coneThetaE};
        const LightCone rc{r.coneAxis, r.coneThetaO, r.coneThetaE};
        const LightCone merged = (l.power <= 0.0f) ? rc : (r.power <= 0.0f) ? lc : ConeUnion(lc, rc);
        node.coneAxis = merged.axis;
        node.coneThetaO = merged.thetaO;
        node.coneThetaE = merged.thetaE;
    }

    return numLeaves;
}

uint32_t BuildLightPowerAlias(const LightInfo* lights, uint32_t count, LightAliasEntry* outEntries)
{
    if (count == 0u) { return 0u; }

    // pdf[i] holds raw power(i) first, then the normalised probability power(i)/total.
    float pdf[MAX_LIGHTS];
    float scaled[MAX_LIGHTS]; // Vose scaled probability = pdf[i] * count
    float prob[MAX_LIGHTS]; // Vose per-bin split threshold
    uint32_t aliasIdx[MAX_LIGHTS];
    uint32_t smallQ[MAX_LIGHTS];
    uint32_t largeQ[MAX_LIGHTS];

    // Power metric mirrors BuildLightBVH (intensity * area * max(colorRGB))
    double total = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        const LightInfo& L = lights[i];
        float area;
        if (L.type == LIGHT_TYPE_SPHERE) {
            const float radius = L.right.w;
            area = 4.0f * LIGHT_BVH_PI * radius * radius;
        }
        else {
            area = 4.0f * L.right.w * L.up.w;
        }
        const float power = L.intensity * area * LightColorMax(L.packedColor);
        pdf[i] = power > 0.0f ? power : 0.0f;
        total += static_cast<double>(pdf[i]);
    }

    // Degenerate (no positive power)
    if (total <= 0.0) {
        const float uniformPdf = 1.0f / static_cast<float>(count);
        for (uint32_t i = 0; i < count; i++) {
            outEntries[i].prob = 1.0f;
            outEntries[i].alias = i;
            outEntries[i].pdf = uniformPdf;
            outEntries[i].pdfAlias = uniformPdf;
        }
        return count;
    }

    const float invTotal = static_cast<float>(1.0 / total);
    for (uint32_t i = 0; i < count; i++) {
        pdf[i] *= invTotal;
        scaled[i] = pdf[i] * static_cast<float>(count);
        aliasIdx[i] = i;
        prob[i] = 1.0f;
    }

    uint32_t nSmall = 0u;
    uint32_t nLarge = 0u;
    for (uint32_t i = 0; i < count; i++) {
        if (scaled[i] < 1.0f) { smallQ[nSmall++] = i; }
        else { largeQ[nLarge++] = i; }
    }

    while (nSmall > 0u && nLarge > 0u) {
        const uint32_t s = smallQ[--nSmall];
        const uint32_t g = largeQ[--nLarge];
        prob[s] = scaled[s];
        aliasIdx[s] = g;
        scaled[g] = (scaled[g] + scaled[s]) - 1.0f;
        if (scaled[g] < 1.0f) { smallQ[nSmall++] = g; }
        else { largeQ[nLarge++] = g; }
    }

    while (nLarge > 0u) { prob[largeQ[--nLarge]] = 1.0f; }
    while (nSmall > 0u) { prob[smallQ[--nSmall]] = 1.0f; }

    for (uint32_t i = 0; i < count; i++) {
        outEntries[i].prob = prob[i];
        outEntries[i].alias = aliasIdx[i];
        outEntries[i].pdf = pdf[i];
        outEntries[i].pdfAlias = pdf[aliasIdx[i]];
    }
    return count;
}
} // namespace Render
