//
// Created by William on 2026-10-31.
//

#ifndef WILL_ENGINE_MATERIAL_STORE_H
#define WILL_ENGINE_MATERIAL_STORE_H

#include "resources/material/material.h"
#include "resources/text_material/text_material.h"
#include "core/containers/array.h"
#include "engine/core/material_id.h"
#include "engine/core/text_material_id.h"
#include "render/render_config.h"
#include "render/interface/render_params.h"
#include "render/shaders/model_interop.h"
#include "render/shaders/text_interop.h"

namespace Engine
{
/** The material definitions and active slots, read-only. A service of its own so reading them does not conflict with a writer of the dirty sets; implementations must not read those sets here. */
class IMaterialStore
{
public:
    static constexpr const char* COMPONENT_NAME = "MaterialStore";

    [[nodiscard]] virtual const Core::Array<MaterialEntry, Render::BINDLESS_MATERIAL_BUFFER_COUNT>& GetActiveMaterials() const = 0;

    [[nodiscard]] virtual RenderMaterial GetRenderMaterial(MaterialID id) const = 0;

    [[nodiscard]] virtual const Material* GetMaterial(MaterialID id) const = 0;

    [[nodiscard]] virtual TextRenderMaterial GetRenderTextMaterial(TextMaterialID id) const = 0;
};
}

#endif //WILL_ENGINE_MATERIAL_STORE_H
