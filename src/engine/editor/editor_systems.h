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

namespace Engine
{
void MarkSceneModified(Engine::EngineState* state, StringID sceneId);

/** Refused while playing or without game.dll. */
void SaveEditorScene(Engine::EngineContext* ctx, Engine::EngineState* state, StringID sceneId);

void SaveModifiedScenes(Engine::EngineContext* ctx, Engine::EngineState* state);

void MarkEntitiesModified(Engine::EngineState* state, Core::Span<entt::entity> entities);

/** Copies each source's subtree into the active scene, renames the top-level copies and selects them. */
void DuplicateEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> sources);

/** Puts the sources' subtrees on the system clipboard as scene text. */
void CopyEntitiesToClipboard(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> sources);

/** Instantiates clipboard entities into the active scene and selects them; non-entity text is ignored. */
void PasteEntitiesFromClipboard(Engine::EngineContext* ctx, Engine::EngineState* state);

bool CanPasteEntities(Engine::EngineContext* ctx);

/** Destroys the selected entities and all their descendants. */
void DeleteSelectedEntities(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Parents the top-level entities under a new group at their centroid, in the first one's folder and parent.
 * @return the new group, or null if nothing could be grouped
 */
entt::entity GroupEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> entities);

void DrawMultiSelectEditor(Engine::EngineContext* ctx, Engine::EngineState* state, const Vec3& centroid, int transformCount);

void EditorUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);

void EditorTickInput(Engine::EngineContext* ctx, Engine::EngineState* state);

void DrawEditorInterface(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
