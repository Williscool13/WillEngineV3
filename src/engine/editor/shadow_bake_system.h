//
// Created by William on 2026-10-09.
//

#ifndef WILL_ENGINE_SHADOW_BAKE_SYSTEM_H
#define WILL_ENGINE_SHADOW_BAKE_SYSTEM_H

#include <entt/entt.hpp>

#include "core/containers/heap_array.h"
#include "core/containers/inline_vector.h"
#include "engine/resources/shadow_map/shadow_map_format.h"
#include "render/interface/render_interface.h"

namespace Engine
{
struct EngineContext;
struct EngineState;

/** Bakes Baked-mode lights' shadow maps one face per request, writing assets/shadows/shadow_<id>.wshadowmap. */
struct ShadowBakeSystem
{
    Core::InlineVector<entt::entity, 256> queue{};
    uint32_t batchTotal{0};
    uint32_t batchDone{0};

    entt::entity entity{entt::null};
    uint64_t shadowId{0};
    LightInfo light{};
    ShadowBakeKey key{};
    uint32_t resolution{0};
    uint32_t face{0};
    uint64_t requestId{0};
    uint64_t nextRequestId{1};
    Core::HeapArray<uint16_t> pixels{};

    void Enqueue(entt::entity light);

    void EnqueueAll(entt::registry& registry);

    void Cancel();

    [[nodiscard]] bool IsBusy() const { return requestId != 0 || !queue.IsEmpty(); }

    void Tick(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer);
};

ShadowBakeKey MakeShadowBakeKey(const LightInfo& light);

/** True when the light still matches the views its map was baked with. */
bool IsShadowBakeCurrent(const ShadowBakeKey& baked, const LightInfo& light);

void ShadowBakeTick(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer);
} // Engine

#endif //WILL_ENGINE_SHADOW_BAKE_SYSTEM_H
