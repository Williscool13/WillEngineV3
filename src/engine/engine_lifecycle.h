//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_ENGINE_LIFECYCLE_H
#define WILL_ENGINE_ENGINE_LIFECYCLE_H

#include "core/types/math.h"
#include "engine/launch/launch_args.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;

inline constexpr Args::ArgDesc ARG_SCENE = Args::StringArg("scene", "", "load this scene (the .wscene stem) instead of the .wplay header's scene or projectConfig.defaultScene");

void CreateCameras(EngineState* state, Vec3 editorPos, Quat editorRot, SystemRegistry& registry);

void CreateDefaultCameras(EngineState* state);

void LoadUIFont(EngineContext* ctx, EngineState* state);

void LoadStartupScene(EngineContext* ctx, EngineState* state);

void HotReloadSave(EngineContext* ctx, EngineState* state);

void HotReloadRestore(EngineContext* ctx, EngineState* state);
} // Engine

#endif //WILL_ENGINE_ENGINE_LIFECYCLE_H
