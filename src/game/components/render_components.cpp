//
// Created by William on 2026-01-30.
//

#include "render_components.h"
#include "render/static_mesh_component.h"

#include <entt/entt.hpp>

#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"

namespace Game::Component
{
void MeshRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto& runtime = registry.get<MeshRuntime>(entity);
    for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
        ctx->materialManager->ReleaseMaterial(runtime.primitives[i].materialID);
    }
    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
    }
}
}
