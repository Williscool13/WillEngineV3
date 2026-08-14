//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PREFAB_FORMAT_H
#define WILL_ENGINE_PREFAB_FORMAT_H

#include <cstdint>
#include <optional>

#include "core/containers/inline_path.h"
#include "core/containers/vector.h"
#include "engine/serialization/text_reader.h"

namespace Engine
{
constexpr uint32_t PREFAB_MAJOR_VERSION = 2;
constexpr uint32_t PREFAB_MINOR_VERSION = 0;
constexpr size_t WPREFAB_NAME_LENGTH = 128;

struct WPrefabHeader
{
    uint64_t prefabId{0};
    char name[WPREFAB_NAME_LENGTH]{};

    uint32_t major{PREFAB_MAJOR_VERSION};
    uint32_t minor{PREFAB_MINOR_VERSION};

    uint64_t contentVersion{0};

    uint32_t componentCount{0};
    uint64_t dataOffset{0};
};

bool WriteWPrefabHeader(Core::Vector<std::byte>& out, const WPrefabHeader& header);

std::optional<WPrefabHeader> ReadWPrefabHeader(const void* data, uint64_t size);

std::optional<WPrefabHeader> ReadWPrefabHeader(const Core::Path& path);

struct WPrefabData
{
    WPrefabHeader header;
    Core::Vector<std::byte> body;

    /** Reader over the text body: one top-level block per component, opener = decimal typeId. */
    TextReader Body() const { return {body.Data(), body.Size()}; }
};

std::optional<WPrefabData> ReadWPrefab(const char* path, Core::TlsfAllocator* alloc);
} // Engine

#endif //WILL_ENGINE_PREFAB_FORMAT_H
