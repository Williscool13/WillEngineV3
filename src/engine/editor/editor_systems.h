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
class SystemRegistry;
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
void SaveEditorScene(Engine::EngineContext* ctx, Engine::EngineState* state, StringID sceneId, Engine::SystemRegistry& registry);

void SaveModifiedScenes(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void MarkEntitiesModified(Engine::EngineState* state, Core::Span<entt::entity> entities, Engine::SystemRegistry& registry);

/** Copies each source's subtree into the active scene, renames the top-level copies and selects them. */
void DuplicateEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> sources, Engine::SystemRegistry& registry);

/** Puts the sources' subtrees on the system clipboard as scene text. */
void CopyEntitiesToClipboard(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> sources, Engine::SystemRegistry& registry);

/** Instantiates clipboard entities into the active scene and selects them; non-entity text is ignored. */
void PasteEntitiesFromClipboard(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

bool CanPasteEntities(Engine::EngineContext* ctx);

/** Destroys the selected entities and all their descendants. */
void DeleteSelectedEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/**
 * Parents the top-level entities under a new group at their centroid, in the first one's folder and parent.
 * @return the new group, or null if nothing could be grouped
 */
entt::entity GroupEntities(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const entt::entity> entities, Engine::SystemRegistry& registry);

/** Creates an empty entity at the parent's origin, in the parent's folder, and selects it. */
entt::entity CreateChildEntity(Engine::EngineState* state, entt::entity parent, Engine::SystemRegistry& registry);

void DrawMultiSelectEditor(Engine::EngineContext* ctx, Engine::EngineState* state, const Vec3& centroid, int transformCount, Engine::SystemRegistry& registry);

void EditorUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void EditorTickInput(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void DrawEditorInterface(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
}

#endif //WILL_ENGINE_EDITOR_SYSTEMS_H
