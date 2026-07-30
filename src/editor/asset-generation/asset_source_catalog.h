//
// Created by William on 2026-07-30.
//

#pragma once

#include <cstdint>

#include "core/containers/inline_path.h"
#include "core/containers/inline_vector.h"

namespace Core
{
class MemoryManager;
}

namespace Editor
{
enum class AssetSourceKind : uint8_t
{
    Model,
    Texture,
    EnvironmentMap,
    Font,
};

enum class AssetOutputState : uint8_t
{
    Missing,
    Outdated,
    Current,
};

struct AssetSourceEntry
{
    AssetSourceKind kind{};
    AssetOutputState state{};
    uint8_t externalIndex{};
    Core::Path sourcePath{};
};

/**
 * Generatable sources found by scanning assets/ (.gltf/.glb, standalone .jpg/.png, .hdr, .ttf). Images in or under a directory containing a model source are excluded.
 */
struct AssetSourceCatalog
{
    Core::InlineVector<AssetSourceEntry, 192> entries;
    uint32_t outdatedCount{0};

    void Scan(Core::MemoryManager& memoryManager);
    static Core::Path OutputPathFor(const AssetSourceEntry& entry);
    static Core::Path TextureOutputDirFor(const AssetSourceEntry& entry);
};
} // Editor
