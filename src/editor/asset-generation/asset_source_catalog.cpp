//
// Created by William on 2026-07-30.
//

#include "asset_source_catalog.h"

#include "core/containers/vector.h"
#include "core/memory/memory_manager.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "engine/resources/font/font_format.h"
#include "engine/resources/model/model_format.h"
#include "engine/resources/texture/texture_format.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Editor
{
struct ExternalModelSource
{
    const char* sourcePath;
    const char* outputRelative;
    const char* textureDirRelative;
};

static constexpr ExternalModelSource EXTERNAL_MODEL_SOURCES[] = {
    {"D:/source/repos/RTXDI-Assets/bistro/bistro.gltf", "models/LumberyardBistro/LumberyardBistro.wsmesh", "models/LumberyardBistro/textures"},
};
static constexpr uint8_t EXTERNAL_MODEL_SOURCE_COUNT = std::size(EXTERNAL_MODEL_SOURCES);

static bool IsUnderDirectory(std::string_view path, std::string_view dir)
{
    return path.size() > dir.size() + 1 && path.substr(0, dir.size()) == dir && (path[dir.size()] == '/' || path[dir.size()] == '\\');
}

static Core::InlineString<300> NormalizePathKey(std::string_view path)
{
    char buf[300];
    const size_t len = std::min(path.size(), sizeof(buf) - 1);
    for (size_t i = 0; i < len; i++) {
        const char c = path[i];
        buf[i] = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    buf[len] = '\0';
    return Core::InlineString<300>(buf);
}

static Core::Path SiblingOutput(const Core::Path& source, const char* wExt)
{
    Core::InlineString<300> name{source.Stem()};
    name.Append(wExt);
    return source.Parent() / name.c_str();
}

static AssetOutputState ComputeOutputState(AssetSourceKind kind, const Core::Path& source, const Core::Path& output)
{
    if (output.IsEmpty()) {
        return AssetOutputState::Missing;
    }
    const uint64_t outputTime = Platform::GetFileWriteTime(output);
    if (outputTime == 0) {
        return AssetOutputState::Missing;
    }
    if (Platform::GetFileWriteTime(source) > outputTime) {
        return AssetOutputState::Outdated;
    }

    switch (kind) {
        case AssetSourceKind::Texture: {
            auto header = Engine::ReadWTextureHeaderAnyVersion(output);
            if (!header || header->major != Engine::TEXTURE_MAJOR_VERSION) { return AssetOutputState::Outdated; }
            break;
        }
        case AssetSourceKind::EnvironmentMap: {
            auto header = Engine::ReadWEnvMapHeaderAnyVersion(output);
            if (!header || header->major != Engine::ENV_MAP_MAJOR_VERSION) { return AssetOutputState::Outdated; }
            break;
        }
        case AssetSourceKind::Font: {
            auto header = Engine::ReadWFontHeaderAnyVersion(output);
            if (!header || header->major != Engine::FONT_MAJOR_VERSION || header->minor > Engine::FONT_MINOR_VERSION) { return AssetOutputState::Outdated; }
            break;
        }
        case AssetSourceKind::Model: {
            auto header = Engine::ReadWStaticModelHeaderAnyVersion(output);
            if (!header || header->version != Engine::STATICMODEL_VERSION) { return AssetOutputState::Outdated; }
            break;
        }
        default: {
            break;
        }
    }
    return AssetOutputState::Current;
}

Core::Path AssetSourceCatalog::OutputPathFor(const AssetSourceEntry& entry)
{
    if (entry.externalIndex != UINT8_MAX) {
        return Platform::GetAssetPath() / EXTERNAL_MODEL_SOURCES[entry.externalIndex].outputRelative;
    }
    if (!entry.outputOverride.IsEmpty()) {
        return entry.outputOverride;
    }
    switch (entry.kind) {
        case AssetSourceKind::Model: {
            return SiblingOutput(entry.sourcePath, ".wsmesh");
        }
        case AssetSourceKind::Texture: {
            return SiblingOutput(entry.sourcePath, ".wtexture");
        }
        case AssetSourceKind::EnvironmentMap: {
            return SiblingOutput(entry.sourcePath, ".wenvmap");
        }
        case AssetSourceKind::Font: {
            const Core::Path fontsRoot = Platform::GetAssetPath() / "fonts";
            const std::string_view full = entry.sourcePath.View();
            if (!IsUnderDirectory(full, fontsRoot.View())) {
                return {};
            }
            const std::string_view below = full.substr(fontsRoot.View().size() + 1);
            const size_t sep = below.find_first_of("/\\");
            if (sep == std::string_view::npos) {
                return {};
            }
            Core::InlineString<128> family{below.substr(0, sep)};
            Core::InlineString<140> output{family};
            output.Append(".wsfont");
            return fontsRoot / family.c_str() / output.c_str();
        }
    }
    return {};
}

Core::Path AssetSourceCatalog::TextureOutputDirFor(const AssetSourceEntry& entry)
{
    if (entry.kind == AssetSourceKind::Model && entry.externalIndex != UINT8_MAX) {
        return Platform::GetAssetPath() / EXTERNAL_MODEL_SOURCES[entry.externalIndex].textureDirRelative;
    }
    return {};
}

void AssetSourceCatalog::Scan(Core::MemoryManager& memoryManager)
{
    entries.Clear();
    outdatedCount = 0;

    const Core::Path assetRoot = Platform::GetAssetPath();
    Core::Vector<Core::Path> paths(&memoryManager.AssetsScratch(), Core::AllocTag::AssetGenerator);
    Platform::RecursiveDirectoryIterator(assetRoot, paths);
    for (uint32_t i = 0; i < paths.Size();) {
        if (paths[i].HasHiddenSegment()) {
            paths.RemoveAt(i);
        }
        else {
            ++i;
        }
    }

    Core::InlineVector<Core::Path, 64> modelDirs;
    for (const Core::Path& path : paths) {
        const std::string_view ext = path.Extension();
        // .obj marks a model directory for image exclusion but is never generatable (importer is glTF only)
        if (ext != ".gltf" && ext != ".glb" && ext != ".obj") {
            continue;
        }
        Core::Path parent = path.Parent();
        if (parent.View() == assetRoot.View() || modelDirs.IsFull()) {
            continue;
        }
        modelDirs.PushBack(parent);
    }

    struct ClaimedSource
    {
        Core::InlineString<300> sourceKey;
        Core::Path output;
    };
    Core::Vector<ClaimedSource> claimed(&memoryManager.AssetsScratch(), Core::AllocTag::AssetGenerator);
    Core::Vector<uint64_t> staleTextureOwners(&memoryManager.AssetsScratch(), Core::AllocTag::AssetGenerator);
    staleModelTextureCount = 0;
    for (const Core::Path& path : paths) {
        if (path.Extension() != ".wtexture") {
            continue;
        }
        auto header = Engine::ReadWTextureHeaderAnyVersion(path);
        if (!header) {
            continue;
        }

        if (header->major != Engine::TEXTURE_MAJOR_VERSION && header->category == Engine::TextureCategory::Model) {
            staleModelTextureCount++;
            if (header->ownerModelId != 0) {
                staleTextureOwners.PushBack(header->ownerModelId);
            }
        }
        if (header->genSource[0] == '\0') {
            continue;
        }
        const Core::Path resolved = path.Parent() / header->genSource;
        claimed.PushBack({NormalizePathKey(resolved.View()), Core::Path(path)});
    }

    auto push = [this, &staleTextureOwners](AssetSourceKind kind, const Core::Path& source, uint8_t externalIndex, const Core::Path& outputOverride = {}) {
        if (entries.IsFull()) {
            LOG_WARN(Asset, "Asset source catalog is full ({} entries); {} skipped", entries.Size(), source.c_str());
            return;
        }
        AssetSourceEntry entry{kind, AssetOutputState::Missing, externalIndex, source, outputOverride};
        const Core::Path output = OutputPathFor(entry);
        entry.state = ComputeOutputState(kind, source, output);
        if (kind == AssetSourceKind::Model && entry.state != AssetOutputState::Missing && !staleTextureOwners.IsEmpty()) {
            if (auto modelHeader = Engine::ReadWStaticModelHeaderAnyVersion(output)) {
                for (const uint64_t owner : staleTextureOwners) {
                    if (owner == modelHeader->modelId) {
                        entry.state = entry.state == AssetOutputState::Outdated ? AssetOutputState::OutdatedAndDerivativeContent : AssetOutputState::DerivativeContentOutdated;
                        break;
                    }
                }
            }
        }
        if (entry.state != AssetOutputState::Current && entry.state != AssetOutputState::Missing && kind != AssetSourceKind::Font) {
            outdatedCount++;
        }
        entries.PushBack(entry);
    };

    for (uint32_t i = 0; i < paths.Size(); ++i) {
        const Core::Path& path = paths[i];
        const std::string_view ext = path.Extension();

        if (ext == ".gltf") {
            push(AssetSourceKind::Model, path, UINT8_MAX);
        }
        else if (ext == ".glb") {
            // A same-stem .gltf/.glb pair is one model; keep the .gltf so two entries never race on one output.
            bool bPaired = false;
            for (uint32_t j = 0; j < paths.Size() && !bPaired; ++j) {
                bPaired = j != i && paths[j].Extension() == ".gltf" && paths[j].Stem() == path.Stem() && paths[j].Parent().View() == path.Parent().View();
            }
            if (!bPaired) {
                push(AssetSourceKind::Model, path, UINT8_MAX);
            }
        }
        else if (ext == ".jpg" || ext == ".png") {
            bool bModelOwned = false;
            for (const Core::Path& dir : modelDirs) {
                if (IsUnderDirectory(path.View(), dir.View())) {
                    bModelOwned = true;
                    break;
                }
            }
            if (!bModelOwned) {
                Core::Path outputOverride{};
                const Core::InlineString<300> key = NormalizePathKey(path.View());
                for (const ClaimedSource& c : claimed) {
                    if (c.sourceKey == key) {
                        outputOverride = c.output;
                        break;
                    }
                }
                push(AssetSourceKind::Texture, path, UINT8_MAX, outputOverride);
            }
        }
        else if (ext == ".hdr") {
            push(AssetSourceKind::EnvironmentMap, path, UINT8_MAX);
        }
        else if (ext == ".ttf") {
            push(AssetSourceKind::Font, path, UINT8_MAX);
        }
    }

    for (uint8_t i = 0; i < EXTERNAL_MODEL_SOURCE_COUNT; ++i) {
        const Core::Path source{EXTERNAL_MODEL_SOURCES[i].sourcePath};
        if (source.Exists()) {
            push(AssetSourceKind::Model, source, i);
        }
    }
}
} // Editor
