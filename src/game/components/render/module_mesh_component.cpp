//
// Created by William on 2026-07-31.
//

#include "module_mesh_component.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "procedural_mesh_component.h"
#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"

namespace Game::Component
{
void ModuleMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<ModuleMeshComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.emplace_or_replace<ModuleMeshLoadPendingTag>(entity);
    state->assetLoad.bPendingModelResolve = true;

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
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

void Component::ModuleMeshComponent::Serialize(const ModuleMeshComponent& comp, nlohmann::json& json)
{
    json["modelFlags"] = {comp.modelFlags.x, comp.modelFlags.y, comp.modelFlags.z, comp.modelFlags.w};
    json["renderOffset"] = {comp.renderOffset.x, comp.renderOffset.y, comp.renderOffset.z};
    json["renderRotation"] = {comp.renderRotation.w, comp.renderRotation.x, comp.renderRotation.y, comp.renderRotation.z};

    nlohmann::json mats = nlohmann::json::array();
    for (int32_t slot = 0; slot < Engine::MAX_MODULE_SLOTS; slot++) {
        mats.push_back(comp.slotMaterials[slot].id);
    }
    json["slotMaterials"] = std::move(mats);

    nlohmann::json parts = nlohmann::json::array();
    for (const Engine::ModulePart& part : comp.params.parts) {
        nlohmann::json pj;
        pj["type"] = part.shape.index();
        SerializeProceduralShape(part.shape, pj);
        pj["offset"] = {part.offset.x, part.offset.y, part.offset.z};
        pj["rotation"] = {part.rotation.w, part.rotation.x, part.rotation.y, part.rotation.z};
        pj["slot"] = part.materialSlot;
        parts.push_back(std::move(pj));
    }
    json["parts"] = std::move(parts);
}

void Component::ModuleMeshComponent::Deserialize(ModuleMeshComponent& comp, const nlohmann::json& json)
{
    if (json.contains("modelFlags")) {
        const auto& f = json["modelFlags"];
        comp.modelFlags = glm::vec4(f[0].get<float>(), f[1].get<float>(), f[2].get<float>(), f[3].get<float>());
    }
    if (json.contains("renderOffset")) {
        const auto& o = json["renderOffset"];
        comp.renderOffset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }
    if (json.contains("renderRotation")) {
        const auto& r = json["renderRotation"];
        comp.renderRotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
    }
    if (json.contains("slotMaterials")) {
        int32_t slot = 0;
        for (const auto& m : json["slotMaterials"]) {
            if (slot >= Engine::MAX_MODULE_SLOTS) { break; }
            comp.slotMaterials[slot++] = Engine::MaterialID(m.get<uint64_t>());
        }
    }
    comp.params.parts.Clear();
    if (json.contains("parts")) {
        for (const auto& pj : json["parts"]) {
            if (comp.params.parts.IsFull()) { break; }
            Engine::ModulePart part{};
            part.shape = DeserializeProceduralShape(pj["type"].get<int32_t>(), pj);
            if (pj.contains("offset")) {
                const auto& o = pj["offset"];
                part.offset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
            }
            if (pj.contains("rotation")) {
                const auto& r = pj["rotation"];
                part.rotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
            }
            part.materialSlot = glm::clamp(pj.value("slot", 0), 0, Engine::MAX_MODULE_SLOTS - 1);
            comp.params.parts.PushBack(part);
        }
    }
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

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        if (ImGui::Checkbox("Visible##modulemesh", &visible)) { component.modelFlags.x = visible ? 1.0f : 0.0f; }
        bool probeBakeExclude = component.modelFlags.y == 0.0f;
        if (ImGui::Checkbox("Probe Bake Exclude##modulemesh", &probeBakeExclude)) { component.modelFlags.y = probeBakeExclude ? 0.0f : 1.0f; }

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
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) { continue; }
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.slotMaterials[slot])) {
                        if (matId != component.slotMaterials[slot]) {
                            component.slotMaterials[slot] = matId;
                            registry.emplace_or_replace<ModuleMeshLoadingTag>(entity);
                            state->assetLoad.bPendingModelResolve = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
    }

    return {.requestRemoval = remove};
}
} // Game
