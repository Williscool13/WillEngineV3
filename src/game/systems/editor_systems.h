//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_EDITOR_SYSTEMS_H
#define WILL_ENGINE_EDITOR_SYSTEMS_H

#include <vector>
#include <entt/entt.hpp>
#include <glm/fwd.hpp>
#include "core/string_id.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
struct ViewFamily;
}

namespace Game
{
void MarkSceneModified(Engine::GameState* state, StringID sceneId);

void MarkEntitiesModified(Engine::GameState* state, const std::vector<entt::entity>& entities);

void DrawMultiSelectEditor(Engine::GameState* state, const glm::vec3& centroid, int transformCount);

void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state);

void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
