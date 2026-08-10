//
// Created by William on 2026-06-22.
//

#include "light_bvh.h"

#include <glm/glm.hpp>

namespace Render
{
static constexpr float LIGHT_BVH_PI = 3.14159265358979323846f;

static float LightColorMax(uint32_t packedColor)
{
    const float r = static_cast<float>(packedColor & 0xFFu) / 255.0f;
    const float g = static_cast<float>((packedColor >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((packedColor >> 16) & 0xFFu) / 255.0f;
    return glm::max(r, glm::max(g, b));
}

uint32_t BuildLightPowerAlias(const LightInfo* lights, uint32_t count, LightAliasScratch& scratch, LightAliasEntry* outEntries)
{
    if (count == 0u) { return 0u; }

    // pdf[i] holds raw power(i) first, then the normalised probability power(i)/total.
    float* pdf = scratch.pdf.Data();
    float* scaled = scratch.scaled.Data(); // Vose scaled probability = pdf[i] * count
    float* prob = scratch.prob.Data(); // Vose per-bin split threshold
    uint32_t* aliasIdx = scratch.aliasIdx.Data();
    uint32_t* smallQ = scratch.smallQ.Data();
    uint32_t* largeQ = scratch.largeQ.Data();

    // Power metric: intensity * area * max(colorRGB)
    double total = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        const LightInfo& L = lights[i];
        float area;
        if (L.type == LIGHT_TYPE_SPHERE) {
            const float radius = L.right.w;
            area = 4.0f * LIGHT_BVH_PI * radius * radius;
        }
        else if (L.type == LIGHT_TYPE_TRIANGLE) {
            area = 0.5f * glm::length(glm::cross(glm::vec3(L.right), glm::vec3(L.up)));
        }
        else {
            area = 4.0f * L.right.w * L.up.w;
        }
        const float power = L.intensity * area * LightColorMax(L.packedColor);
        pdf[i] = power > 0.0f ? power : 0.0f;
        total += static_cast<double>(pdf[i]);
    }

    if (count == scratch.prevCount && memcmp(pdf, scratch.prevPower.Data(), count * sizeof(float)) == 0) {
        memcpy(outEntries, scratch.cachedEntries.Data(), count * sizeof(LightAliasEntry));
        return count;
    }
    memcpy(scratch.prevPower.Data(), pdf, count * sizeof(float));
    scratch.prevCount = count;
    LightAliasEntry* entries = scratch.cachedEntries.Data();

    // Degenerate (no positive power)
    if (total <= 0.0) {
        const float uniformPdf = 1.0f / static_cast<float>(count);
        for (uint32_t i = 0; i < count; i++) {
            entries[i].prob = 1.0f;
            entries[i].alias = i;
            entries[i].pdf = uniformPdf;
            entries[i].pdfAlias = uniformPdf;
        }
        memcpy(outEntries, entries, count * sizeof(LightAliasEntry));
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
        entries[i].prob = prob[i];
        entries[i].alias = aliasIdx[i];
        entries[i].pdf = pdf[i];
        entries[i].pdfAlias = pdf[aliasIdx[i]];
    }
    memcpy(outEntries, entries, count * sizeof(LightAliasEntry));
    return count;
}
} // namespace Render
