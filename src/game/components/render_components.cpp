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

namespace Game::Component
{
void MeshRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get<MeshRuntime>(entity);

    if (runtime.range.IsValid()) {
        Engine::MeshPrimitiveStore& store = state->meshPrimitiveStore;
        for (uint32_t i = 0; i < runtime.range.count; ++i) {
            ctx->materialManager->ReleaseMaterial(store[runtime.range.offset + i].materialID);
        }
        store.Free(runtime.range);
        runtime.range = {};
    }
    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
    }
}
}
