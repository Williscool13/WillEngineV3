//
// Created by William on 2025-12-15.
//

#include "model_generator.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include "spdlog/spdlog.h"


namespace Render
{
void ModelGenerator::LoadGltf(const std::filesystem::path& source)
{
    fastgltf::Parser parser{fastgltf::Extensions::KHR_texture_basisu | fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_transform};
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
                                 | fastgltf::Options::AllowDouble
                                 | fastgltf::Options::LoadExternalBuffers
                                 | fastgltf::Options::LoadExternalImages;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(source);
    if (!static_cast<bool>(gltfFile)) {
        SPDLOG_ERROR("Failed to open glTF file ({}): {}\n", source.filename().string(), getErrorMessage(gltfFile.error()));
        return;
    }

    auto load = parser.loadGltf(gltfFile.get(), source.parent_path(), gltfOptions);
    if (!load) {
        SPDLOG_ERROR("Failed to load glTF: {}\n", to_underlying(load.error()));
        return;
    }

    bool bIsSkeletalMesh = false;
    bool bSuccessfullyLoaded = true;
    auto name = source.filename().string();
    fastgltf::Asset gltf = std::move(load.get());
}
} // Render