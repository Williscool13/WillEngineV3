//
// Created by William on 2026-08-29.
//

#include "render_systems_helpers.h"

#ifdef WDEBUG

#include <cstring>

#include <tracy/Tracy.hpp>

#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/components/core_components.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/reflection_probe_component.h"

namespace Engine
{
void VerifyAnalyticLightStore(Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (!state->debug.bVerifyStoresOnce) { return; }
    ZoneScoped;
    const LightInfo* lights = registry.Service<Engine::AnalyticLightStore>().Lights();
    uint32_t staleCount = 0;
    uint32_t firstStaleSlot = 0;

    for (const auto& [entity, light, runtime, transform] : registry.View<Component::AreaLightComponent, Component::AreaLightRuntime, Component::RenderWorldTransformRuntime>(entt::exclude<Component::ProbeBakeHiddenFlag>).each()) {
        if (runtime.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
        const LightInfo expected = Component::ComputeAreaLightInfo(transform, light);
        if (memcmp(&expected, &lights[runtime.lightSlot], sizeof(LightInfo)) != 0) {
            if (staleCount++ == 0) { firstStaleSlot = runtime.lightSlot; }
        }
    }

    for (const auto& [entity, light, runtime, transform] : registry.View<Component::SphereLightComponent, Component::SphereLightRuntime, Component::RenderWorldTransformRuntime>(entt::exclude<Component::ProbeBakeHiddenFlag>).each()) {
        if (runtime.lightSlot == Engine::AnalyticLightStore::INVALID_SLOT) { continue; }
        const LightInfo expected = Component::ComputeSphereLightInfo(transform, light);
        if (memcmp(&expected, &lights[runtime.lightSlot], sizeof(LightInfo)) != 0) {
            if (staleCount++ == 0) { firstStaleSlot = runtime.lightSlot; }
        }
    }

    if (staleCount > 0) {
        LOG_ERROR(Engine, "{} analytic light(s) stale in the store, first at slot {}; a mutation did not mark MultiframeDirtyRequest", staleCount, firstStaleSlot);
    }
    else {
        LOG_INFO(Engine, "Analytic light store clean");
    }
}

void VerifyGeometryStores(Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    if (!state->debug.bVerifyStoresOnce) { return; }
    ZoneScoped;

    Engine::InstanceStore& instanceStore = registry.ServiceMut<Engine::InstanceStore>();
    Engine::TriLightStore& triLightStore = registry.ServiceMut<Engine::TriLightStore>();
    const uint32_t stale = instanceStore.VerifyRecords(&triLightStore);
    if (stale > 0) {
        LOG_ERROR(Engine, "{} instance record(s) stale in the store; a source mutation did not go through WriteRecord", stale);
    }
    else {
        LOG_INFO(Engine, "Instance store clean");
    }

    instanceStore.MarkAllDirty();
    registry.ServiceMut<Engine::ModelStore>().MarkAllDirty();
    triLightStore.MarkAllDirty();
}
} // Engine

#endif
