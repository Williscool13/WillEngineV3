//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include "imgui.h"

#include "engine/component_registry.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void PrefabInstanceComponent::Serialize(const PrefabInstanceComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("prefabId", comp.prefabId.id, uint64_t{0});
    w.KeyOpt("bMasterPrefab", comp.bMasterPrefab, false);
}

void PrefabInstanceComponent::Deserialize(PrefabInstanceComponent& comp, const Engine::TextReader& r)
{
    comp.prefabId = StringID(r.U64("prefabId", comp.prefabId.id));
    comp.bMasterPrefab = r.Bool("bMasterPrefab", comp.bMasterPrefab);
}

Engine::ComponentEditorResult PrefabInstanceComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& comp = registry.get<PrefabInstanceComponent>(entity);

    bool open = ImGui::CollapsingHeader("Prefab Instance##componentprefab", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteprefab");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        ImGui::TextDisabled("ID: %llu", comp.prefabId.id);
        if (ImGui::Checkbox("Master Prefab", &comp.bMasterPrefab)) {
            modified = true;
            if (comp.bMasterPrefab) {
                auto view = registry.view<PrefabInstanceComponent>();
                for (auto e : view) {
                    if (e == entity) { continue; }
                    auto& other = view.get<PrefabInstanceComponent>(e);
                    if (other.prefabId == comp.prefabId) {
                        other.bMasterPrefab = false;
                    }
                }
            }
        }
    }
    return {.bRequestRemoval = remove, .bModified = modified};
}

void NameComponent::Serialize(const NameComponent& comp, Engine::TextWriter& w)
{
    if (!comp.name.IsEmpty()) {
        w.KeyStr("name", comp.name.View());
    }
}

void NameComponent::Deserialize(NameComponent& comp, const Engine::TextReader& r)
{
    r.Str("name", comp.name);
}

Engine::ComponentEditorResult NameComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<NameComponent>(entity);
    bool open = ImGui::CollapsingHeader("Name##componentname", ImGuiTreeNodeFlags_DefaultOpen);

    bool modified = false;
    if (open) {
        char buf[128];
        strncpy_s(buf, component.name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            component.name = Core::InlineString<128>(buf);
            modified = true;
        }
    }
    return {.bModified = modified};
}
} // Game::Component
