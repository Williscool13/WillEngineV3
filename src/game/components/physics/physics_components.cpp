//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include <glm/gtc/type_ptr.hpp>


#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/fwd_components.h"
#include "game/systems/physics_system.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "physics/physics_system.h"

#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_editor.h"
#include "physics_body_desc.h"
#include "physics_body_component.h"

namespace Game
{


ComponentEditorResult Component::DrawPhysicsDebugTag::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                          const char* name)
{
    ImGui::CollapsingHeader("Physics Debug Draw", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletephysicscomponent");
    ImGui::PopStyleColor();

    return {.requestRemoval = remove};
}
}
