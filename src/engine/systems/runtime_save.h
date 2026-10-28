//
// Created by William on 2026-10-25.
//

#ifndef WILL_ENGINE_RUNTIME_SAVE_H
#define WILL_ENGINE_RUNTIME_SAVE_H

#include <cstdint>

#include "core/string_id.h"
#include "core/containers/inline_string.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;

struct RuntimeSceneStatus
{
    bool bExists{false};
    bool bUnreadable{false};
    /** Saved entities gone from the scene. */
    uint32_t missing{0};
    /** Saved entities whose authored components changed since. */
    uint32_t changed{0};
    Core::InlineString<128> firstChanged{};

    bool IsOutdated() const { return bUnreadable || missing + changed > 0; }
};

/** While running. Authored entities: pose and runtime state; destroyed ones by id. Spawned entities: in full. Plus rng and run clock. */
bool SaveRuntimeScene(EngineContext* ctx, EngineState* state, SystemRegistry& registry, StringID sceneId);

bool DeleteRuntimeScene(EngineContext* ctx, EngineState* state, SystemRegistry& registry, StringID sceneId);

/** Loaded scenes only; cached for 500 ms. */
const RuntimeSceneStatus& GetRuntimeSceneStatus(EngineContext* ctx, EngineState* state, SystemRegistry& registry, StringID sceneId);

/** Over each loaded scene. */
void ApplyRuntimeScenes(EngineContext* ctx, EngineState* state, SystemRegistry& registry);
} // Engine

#endif //WILL_ENGINE_RUNTIME_SAVE_H
