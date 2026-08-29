//
// Created by William on 2026-08-29.
//

#ifndef WILL_ENGINE_RENDER_SYSTEMS_HELPERS_H
#define WILL_ENGINE_RENDER_SYSTEMS_HELPERS_H

namespace Engine
{
struct EngineState;
}

namespace Game
{
#ifdef WDEBUG
/** Catches a light mutated without marking MultiframeDirtyComponent, which nothing else would notice until the light looked wrong. No-op unless DebugState::bVerifyStoresOnce is raised. */
void VerifyAnalyticLightStore(Engine::EngineState* state);

/**
 * Instances are checked by derivation: their record is a pure projection of InstanceSource, so a source mutated without WriteRecord shows up directly.
 * Model matrices are not derivable without duplicating RenderPrepareTransforms, so both stores instead re-send everything live. A missed mark shows as geometry correcting itself the moment this runs. No-op unless DebugState::bVerifyStoresOnce is raised.
 */
void VerifyGeometryStores(Engine::EngineState* state);
#endif
} // Game

#endif //WILL_ENGINE_RENDER_SYSTEMS_HELPERS_H
