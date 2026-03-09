//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_MATERIALS_H
#define WILL_ENGINE_MATERIALS_H
#include <filesystem>
#include <string>
#include <json/nlohmann/json.hpp>

#include "core/string_id.h"
#include "core/allocators/handle.h"
#include "engine/core/material_id.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
static constexpr int32_t WMATERIAL_VERSION = 1;
using MaterialEntryHandle = Core::Handle<MaterialProperties>;

struct MaterialEntry
{
    MaterialID id{MaterialID::INVALID};
    MaterialEntryHandle handle{MaterialEntryHandle::INVALID};
    int32_t refCounter{0};

    // When ref counter is 0, if retireFrame >= renderFrame, then this will be deleted.
    uint64_t retireFrame{INT64_MAX};
};

struct Material
{
    std::string name;
    MaterialID id;
    MaterialProperties props;

    StringID pipelineID;
    std::filesystem::path sourcePath; // useless if mutable

    bool immutable{false};
};

MaterialID HashMaterial(const MaterialProperties& m);

nlohmann::json SerializeMaterial(const Material& mat);

Material DeserializeMaterial(const nlohmann::json& j, const std::filesystem::path& sourcePath);
} // Engine

#endif //WILL_ENGINE_MATERIALS_H
