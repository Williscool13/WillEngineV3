//
// Created by William on 2026-07-31.
//

#include "module_mesh_component.h"

#include "imgui.h"

#include "mesh_source_exclusion.h"
#include "procedural_mesh_component.h"
#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/editor/editor_materials.h"
#include "engine/components/core_components.h"
#include "engine/editor/edit_context.h"

namespace Engine::Component
{
void ModuleMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<ModuleMeshComponent>(entity);

    registry.emplace_or_replace<ModuleMeshLoadPendingTag>(entity);

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void ModuleMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<ModuleMeshLoadPendingTag>(entity);
    registry.remove<ModuleMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
}
}

namespace Engine
{
bool Component::ModuleMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return Component::MeshSources::NoneOtherThan<Component::ModuleMeshComponent>(registry, entity);
}

void Component::ModuleMeshComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<ModuleMeshLoadingTag>(entity);
}

Engine::ComponentEditorResult Component::ModuleMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const auto& component = edit.Get<ModuleMeshComponent>();
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = edit.State();

    bool open = ImGui::CollapsingHeader("Module Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletemodulemesh");
    ImGui::PopStyleColor();

    if (open) {
        if (DrawRenderFlagToggles(edit, RENDER_TOGGLE_VISIBLE | RENDER_TOGGLE_EMISSIVE)) {
            edit.ForEachTarget<ModuleMeshComponent>([&registry](entt::entity e) { registry.emplace_or_replace<ModuleMeshLoadingTag>(e); });
        }

        // Parts are script-authored; the editor only re-skins slots
        int32_t maxSlot = -1;
        for (const Engine::ModulePart& part : component.params.parts) {
            maxSlot = glm::max(maxSlot, part.materialSlot);
        }
        ImGui::Text("%u parts, %d slots", static_cast<uint32_t>(component.params.parts.Size()), maxSlot + 1);

        for (int32_t slot = 0; slot <= maxSlot; slot++) {
            ImGui::PushID(slot);
            const char* currentLabel = "(default)";
            if (component.slotMaterials[slot].IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.slotMaterials[slot])) {
                    currentLabel = m->name.c_str();
                }
            }
            Core::InlineString<32> label = Core::InlineString<32>::Format("Slot %d", slot);
            if (ImGui::BeginCombo(label.c_str(), currentLabel, ImGuiComboFlags_HeightLarge)) {
                if (ImGui::Selectable("(default)", !component.slotMaterials[slot].IsValid())) {
                    edit.Modify<ModuleMeshComponent>([slot](ModuleMeshComponent& c) { c.slotMaterials[slot] = Engine::MaterialID{}; });
                }
                const Engine::MaterialID picked = Engine::DrawMaterialSelector(ctx, state, state->editor.materialSelector, component.slotMaterials[slot]);
                if (picked.IsValid() && picked != component.slotMaterials[slot]) {
                    edit.Modify<ModuleMeshComponent>([slot, picked](ModuleMeshComponent& c) { c.slotMaterials[slot] = picked; });
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
    }

    return {.bRequestRemoval = remove};
}
} // Engine
