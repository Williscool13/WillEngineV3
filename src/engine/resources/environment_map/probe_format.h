//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_PROBE_FORMAT_H
#define WILL_ENGINE_PROBE_FORMAT_H

#include <cstdint>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"
#include "engine/compression/compression.h"
#include "engine/resources/environment_map/environment_map_format.h"

namespace Engine
{
constexpr uint32_t PROBE_MAJOR_VERSION = 0;
constexpr uint32_t PROBE_MINOR_VERSION = 1;

/** Baked-state snapshot captured at bake time, used to detect a stale bake against the live component. Rotation is stored w,x,y,z. */
struct ProbeBakeSnapshot
{
    float translation[3]{};
    float rotation[4]{1.0f, 0.0f, 0.0f, 0.0f};
    float scale[3]{1.0f, 1.0f, 1.0f};
    float captureOffset[3]{};
};

/** Header for a .wprobe asset. Carries the full WEnvMapHeader payload description (so the shared cubemap load path works unchanged) plus probe identity and the bake-time snapshot. */
struct WProbeHeader
{
    uint64_t probeId{0};
    uint64_t environmentMapId{0};
    char name[WENVMAP_NAME_LENGTH]{};

    uint32_t major{PROBE_MAJOR_VERSION};
    uint32_t minor{PROBE_MINOR_VERSION};

    uint64_t contentVersion{0};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipCount{0};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    CompressionType compressionType{DEFAULT_ENV_MAP_COMPRESSION};

    uint32_t resolution{0};
    ProbeBakeSnapshot snapshot{};
};

bool WriteWProbeHeader(std::ostream& out, const WProbeHeader& header);

std::optional<WProbeHeader> ReadWProbeHeader(std::istream& in);

std::optional<WProbeHeader> ReadWProbeHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_PROBE_FORMAT_H
