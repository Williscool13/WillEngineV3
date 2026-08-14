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
void RenderFlagsComponent::Serialize(const RenderFlagsComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("flags", comp.flags, DEFAULT_FLAGS);
}

void RenderFlagsComponent::Deserialize(RenderFlagsComponent& comp, const Engine::TextReader& r)
{
    comp.flags = r.UInt("flags", comp.flags);
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
