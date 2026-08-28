//
// Created by William on 2026-01-30.
//

#include "render_components.h"
#include "render/static_mesh_component.h"

#include <entt/entt.hpp>

#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

namespace Game::Component
{
static constexpr struct { const char* key; uint32_t bit; } RENDER_FLAG_KEYS[] = {
    {"visible", RenderFlagsComponent::VISIBLE},
    {"probeBake", RenderFlagsComponent::PROBE_BAKE_INCLUDE},
    {"ddgi", RenderFlagsComponent::DDGI_CONTRIBUTE},
    {"motionBlur", RenderFlagsComponent::MOTION_BLUR},
    {"alphaCutout", RenderFlagsComponent::ALPHA_CUTOUT},
    {"emissiveLight", RenderFlagsComponent::EMISSIVE_LIGHT},
};

void RenderFlagsComponent::Serialize(const RenderFlagsComponent& comp, Engine::TextWriter& w)
{
    for (const auto& f : RENDER_FLAG_KEYS) {
        w.KeyOpt(f.key, comp.Has(f.bit), (DEFAULT_FLAGS & f.bit) != 0);
    }
}

void RenderFlagsComponent::Deserialize(RenderFlagsComponent& comp, const Engine::TextReader& r)
{
    for (const auto& f : RENDER_FLAG_KEYS) {
        comp.Set(f.bit, r.Bool(f.key, (DEFAULT_FLAGS & f.bit) != 0));
    }
}

void MeshRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get<MeshRuntime>(entity);

    state->instanceStore.ReleaseAndFree(ctx->materialManager, &state->triLightStore, runtime.range);
    state->modelStore.Free(runtime.modelRange);
    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
    }
}
}
