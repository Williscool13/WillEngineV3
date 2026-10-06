//
// Created by William on 2026-10-06.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_HANDLES_H
#define WILL_ENGINE_RENDER_GRAPH_HANDLES_H

#include <cstdint>

#include "core/containers/array.h"

namespace Render
{
inline constexpr uint32_t RDG_MAX_RING_DEPTH = 4;
inline constexpr uint32_t RDG_INVALID_INDEX = UINT32_MAX;

struct RDGTexture
{
    uint32_t index{RDG_INVALID_INDEX};

    [[nodiscard]] bool IsValid() const { return index != RDG_INVALID_INDEX; }
    bool operator==(const RDGTexture&) const = default;
};

struct RDGBuffer
{
    uint32_t index{RDG_INVALID_INDEX};

    [[nodiscard]] bool IsValid() const { return index != RDG_INVALID_INDEX; }
    bool operator==(const RDGBuffer&) const = default;
};

template<typename T>
struct RDGRing
{
    Core::Array<T, RDG_MAX_RING_DEPTH + 1> versions{};
    uint32_t ringIndex{RDG_INVALID_INDEX};

    [[nodiscard]] T Current() const { return versions[0]; }
    [[nodiscard]] T Version(uint32_t age) const { return versions[age]; }
};

using RDGTextureRing = RDGRing<RDGTexture>;
using RDGBufferRing = RDGRing<RDGBuffer>;
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_HANDLES_H
