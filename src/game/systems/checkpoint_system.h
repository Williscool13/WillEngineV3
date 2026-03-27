//
// Created by William on 2026-03-27.
//

#ifndef WILL_ENGINE_CHECKPOINT_SYSTEM_H
#define WILL_ENGINE_CHECKPOINT_SYSTEM_H

namespace Core { struct EngineContext; }
namespace Engine { struct GameState; }

namespace Game
{
void CheckpointUpdate(Core::EngineContext* ctx, Engine::GameState* state);
} // Game

#endif //WILL_ENGINE_CHECKPOINT_SYSTEM_H
