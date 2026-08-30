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
#include "game/systems/render_systems.h"

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
        if (r.Bool(f.key, (DEFAULT_FLAGS & f.bit) != 0)) { comp.flags |= f.bit; }
        else { comp.flags &= ~f.bit; }
    }
}

void SetRenderFlag(Engine::EngineState* state, entt::entity entity, RenderFlagsComponent& renderFlags, uint32_t bit, bool value)
{
    if (value) { renderFlags.flags |= bit; }
    else { renderFlags.flags &= ~bit; }

    EvaluateInstanceRenderState(state, entity);
}

void MeshRuntime::OnConstruct(entt::registry& registry, entt::entity entity)
{
    if (const auto* stable = registry.try_get<StableIdComponent>(entity)) {
        registry.get<MeshRuntime>(entity).stableId = stable->id.id;
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
