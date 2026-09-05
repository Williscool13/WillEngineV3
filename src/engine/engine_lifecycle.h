//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_ENGINE_LIFECYCLE_H
#define WILL_ENGINE_ENGINE_LIFECYCLE_H

#include "core/types/math.h"

namespace Engine
{
struct EngineContext;
struct EngineState;

void CreateCameras(EngineState* state, Vec3 editorPos, Quat editorRot);

void CreateDefaultCameras(EngineState* state);

void LoadUIFont(EngineContext* ctx, EngineState* state);

void LoadStartupScene(EngineContext* ctx, EngineState* state);

void HotReloadSave(EngineContext* ctx, EngineState* state);

void HotReloadRestore(EngineContext* ctx, EngineState* state);
} // Engine

#endif //WILL_ENGINE_ENGINE_LIFECYCLE_H
