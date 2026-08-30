//
// Created by William on 2026-07-31.
//

#include "module_mesh_component.h"

#include "imgui.h"

#include "procedural_mesh_component.h"
#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/components/core_components.h"

namespace Game::Component
{
void ModuleMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<ModuleMeshComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.emplace_or_replace<ModuleMeshLoadPendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;

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

namespace Game
{
bool Component::ModuleMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::ProceduralMeshComponent, Component::SplineMeshComponent, Component::Text3DComponent>(entity);
}

void Component::ModuleMeshComponent::Serialize(const ModuleMeshComponent& comp, Engine::TextWriter& w)
{
    static const ModuleMeshComponent DEF{};
    w.KeyOpt("renderOffset", comp.renderOffset, DEF.renderOffset);
    w.KeyOpt("renderRotation", comp.renderRotation, DEF.renderRotation);

    w.Count("slotMaterials", Engine::MAX_MODULE_SLOTS);
    for (int32_t slot = 0; slot < Engine::MAX_MODULE_SLOTS; slot++) {
        w.BeginBlock("s");
        w.KeyOpt("id", comp.slotMaterials[slot].id, Engine::MaterialID::INVALID.id);
        w.EndBlock();
    }

    if (!comp.params.parts.IsEmpty()) {
        w.Count("parts", static_cast<uint32_t>(comp.params.parts.Size()));
        for (const Engine::ModulePart& part : comp.params.parts) {
            w.BeginBlock("p");
            w.Key("type", static_cast<uint32_t>(part.shape.index()));
            SerializeProceduralShape(part.shape, w);
            w.Key("offset", part.offset);
            w.Key("rotation", part.rotation);
            w.Key("slot", part.materialSlot);
            w.EndBlock();
        }
    }
}

void Component::ModuleMeshComponent::Deserialize(ModuleMeshComponent& comp, const Engine::TextReader& r)
{
    comp.renderOffset = r.Vec3("renderOffset", comp.renderOffset);
    comp.renderRotation = r.Quat("renderRotation", comp.renderRotation);

    int32_t slot = 0;
    r.ForEachRecord("slotMaterials", [&](const Engine::TextReader& s) {
        if (slot >= Engine::MAX_MODULE_SLOTS) { return; }
        comp.slotMaterials[slot++] = Engine::MaterialID(s.U64("id", Engine::MaterialID::INVALID.id));
    });

    comp.params.parts.Clear();
    r.ForEachRecord("parts", [&](const Engine::TextReader& p) {
        if (comp.params.parts.IsFull()) { return; }
        Engine::ModulePart part{};
        part.shape = DeserializeProceduralShape(p.Int("type", 0), p);
        part.offset = p.Vec3("offset", part.offset);
        part.rotation = p.Quat("rotation", part.rotation);
        part.materialSlot = glm::clamp(p.Int("slot", 0), 0, Engine::MAX_MODULE_SLOTS - 1);
        comp.params.parts.PushBack(part);
    });
}

Engine::ComponentEditorResult Component::ModuleMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<ModuleMeshComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    bool open = ImGui::CollapsingHeader("Module Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletemodulemesh");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& renderFlags = registry.get_or_emplace<RenderFlagsComponent>(entity);
        bool visible = renderFlags.Has(RenderFlagsComponent::VISIBLE);
        if (ImGui::Checkbox("Visible##modulemesh", &visible)) { SetRenderFlag(state, entity, renderFlags, RenderFlagsComponent::VISIBLE, visible); }
        bool probeBakeExclude = !renderFlags.Has(RenderFlagsComponent::PROBE_BAKE_INCLUDE);
        if (ImGui::Checkbox("Probe Bake Exclude##modulemesh", &probeBakeExclude)) { SetRenderFlag(state, entity, renderFlags, RenderFlagsComponent::PROBE_BAKE_INCLUDE, !probeBakeExclude); }
        bool emissiveLight = renderFlags.Has(RenderFlagsComponent::EMISSIVE_LIGHT);
        if (ImGui::Checkbox("Emissive Light##modulemesh", &emissiveLight)) {
            SetRenderFlag(state, entity, renderFlags, RenderFlagsComponent::EMISSIVE_LIGHT, emissiveLight);
            registry.emplace_or_replace<ModuleMeshLoadingTag>(entity);
            state->assetLoad.bPendingModelResolve = true;
            modified = true;
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
            if (ImGui::BeginCombo(label.c_str(), currentLabel)) {
                if (ImGui::Selectable("(default)", !component.slotMaterials[slot].IsValid())) {
                    if (component.slotMaterials[slot].IsValid()) {
                        component.slotMaterials[slot] = Engine::MaterialID{};
                        registry.emplace_or_replace<ModuleMeshLoadingTag>(entity);
                        state->assetLoad.bPendingModelResolve = true;
                        modified = true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.bSynthesized) { continue; }
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.slotMaterials[slot])) {
                        if (matId != component.slotMaterials[slot]) {
                            component.slotMaterials[slot] = matId;
                            registry.emplace_or_replace<ModuleMeshLoadingTag>(entity);
                            state->assetLoad.bPendingModelResolve = true;
                            modified = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game
