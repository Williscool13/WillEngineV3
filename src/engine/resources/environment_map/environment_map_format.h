//
// Created by William on 2026-04-15.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_FORMAT_H
#define WILL_ENGINE_ENVIRONMENT_MAP_FORMAT_H

#include <cstdint>
#include <iosfwd>
#include <optional>

#include "core/containers/inline_path.h"
#include "engine/compression/compression.h"

namespace Engine
{
constexpr uint32_t ENV_MAP_MAJOR_VERSION = 0;
constexpr uint32_t ENV_MAP_MINOR_VERSION = 1;
constexpr size_t WENVMAP_NAME_LENGTH = 128;

struct WEnvMapHeader
{
    uint64_t environmentMapId{0};
    char name[WENVMAP_NAME_LENGTH]{};

    uint32_t major{ENV_MAP_MAJOR_VERSION};
    uint32_t minor{ENV_MAP_MINOR_VERSION};

    uint64_t contentVersion{0};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t mipCount{0};
    uint64_t dataOffset{0};
    uint64_t dataSize{0};
    uint64_t uncompressedSize{0};
    CompressionType compressionType{DEFAULT_ENV_MAP_COMPRESSION};
};

bool WriteWEnvMapHeader(std::ostream& out, const WEnvMapHeader& header);

std::optional<WEnvMapHeader> ReadWEnvMapHeader(std::istream& in);

std::optional<WEnvMapHeader> ReadWEnvMapHeader(const Core::Path& path);
} // Engine

#endif //WILL_ENGINE_ENVIRONMENT_MAP_FORMAT_H
