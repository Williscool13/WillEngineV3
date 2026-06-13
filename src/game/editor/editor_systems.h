//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_EDITOR_SYSTEMS_H
#define WILL_ENGINE_EDITOR_SYSTEMS_H

#include <entt/entt.hpp>
#include "core/string_id.h"
#include "core/containers/span.h"
#include "core/types/math.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
struct ViewFamily;
}

namespace Game
{
void MarkSceneModified(Engine::EngineState* state, StringID sceneId);

void MarkEntitiesModified(Engine::EngineState* state, Core::Span<entt::entity> entities);

void DrawMultiSelectEditor(Engine::EngineContext* ctx, Engine::EngineState* state, const Vec3& centroid, int transformCount);

void EditorUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawEditorInterface(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
