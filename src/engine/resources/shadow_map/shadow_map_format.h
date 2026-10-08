//
// Created by William on 2026-10-09.
//

#ifndef WILL_ENGINE_SHADOW_MAP_FORMAT_H
#define WILL_ENGINE_SHADOW_MAP_FORMAT_H

#include <cstdint>
#include <optional>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"
#include "engine/compression/compression.h"

namespace Engine
{
constexpr uint32_t SHADOW_MAP_MAJOR_VERSION = 1;
constexpr uint32_t SHADOW_MAP_MINOR_VERSION = 0;

/** A bake is stale when the live light gives a different key. */
struct ShadowBakeKey
{
    float eye[3]{};
    float forward[3]{};
    float tanHalf{0.0f};
    float nearPlane{0.0f};
    float farPlane{0.0f};
    uint32_t faceCount{0};
};

/** Payload: Zstd WImage, faceCount R16 layers of (viewDepth - near) / (far - near). */
struct WShadowMapHeader
{
    uint64_t shadowId{0};
    uint32_t major{SHADOW_MAP_MAJOR_VERSION};
    uint32_t minor{SHADOW_MAP_MINOR_VERSION};
    uint64_t contentVersion{0};
    uint32_t resolution{0};
    ShadowBakeKey key{};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    CompressionType compressionType{CompressionType::Zstd};
};

bool WriteWShadowMapHeader(Core::Vector<std::byte>& out, const WShadowMapHeader& header);
std::optional<WShadowMapHeader> ReadWShadowMapHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_SHADOW_MAP_FORMAT_H
