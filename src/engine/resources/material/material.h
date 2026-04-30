//
// Created by William on 2026-03-09.
//

#ifndef WILL_ENGINE_MATERIALS_H
#define WILL_ENGINE_MATERIALS_H
#include <string>
#include <json/nlohmann/json.hpp>

#include "core/string_id.h"
#include "core/containers/inline_path.h"
#include "core/containers/inline_string.h"
#include "core/memory/handle.h"
#include "engine/core/material_id.h"
#include "engine/core/texture_id.h"
#include "engine/resources/sampler/sampler.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
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
    // .wmaterial related properties. Only relevant for user defined materials
    Core::InlineString<128> name;
    MaterialID id;
    Core::Path sourcePath;

    // The MEAT of the material
    MaterialProperties props{};
    TextureID textureRefs[6];
    SamplerDesc samplerDesc[6];
    bool bIsRuntimeLoaded{false};


    /**
     * Expected pipeline to draw this material with (not used at the moment)
     */
    StringID pipelineID;

    // Runtime specific fields
    bool immutable{false};
};

MaterialID HashMaterial(const Material& m);

nlohmann::json SerializeMaterial(const Material& mat);

Material DeserializeMaterial(const nlohmann::json& j, const Core::Path& sourcePath);
} // Engine

#endif //WILL_ENGINE_MATERIALS_H
