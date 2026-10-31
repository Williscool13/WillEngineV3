//
// Created by William on 2026-10-28.
//

#include "system_registry.h"

#include "engine/engine_api.h"
#include "engine/systems/command_queue.h"

namespace Engine
{
void SystemRegistry::ApplyPushCommand(Registry& registry, entt::entity, void* payload)
{
    registry.State()->commandQueue.Push(*static_cast<const Command*>(payload));
}

void SystemRegistry::PushCommand(const Command& command)
{
    if (commands != nullptr) {
        commands->Record(&ApplyPushCommand, command.entity, command);
        return;
    }
    registry.State()->commandQueue.Push(command);
}
} // Engine
