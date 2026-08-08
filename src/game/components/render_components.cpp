//
// Created by William on 2026-01-30.
//

#include "render_components.h"
#include "render/static_mesh_component.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"

namespace Game::Component
{
void RenderFlagsComponent::Serialize(const RenderFlagsComponent& comp, nlohmann::json& json)
{
    json["flags"] = comp.flags;
}

void RenderFlagsComponent::Deserialize(RenderFlagsComponent& comp, const nlohmann::json& json)
{
    comp.flags = json.value("flags", DEFAULT_FLAGS);
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
