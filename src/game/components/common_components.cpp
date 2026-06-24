//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void PrefabInstanceComponent::Serialize(const PrefabInstanceComponent& comp, nlohmann::json& json)
{
    json["prefabId"] = comp.prefabId.id;
    json["bMasterPrefab"] = comp.bMasterPrefab;
}

void PrefabInstanceComponent::Deserialize(PrefabInstanceComponent& comp, const nlohmann::json& json)
{
    comp.prefabId = StringID(json["prefabId"].get<uint64_t>());
    comp.bMasterPrefab = json.value("bMasterPrefab", false);
}

Engine::ComponentEditorResult PrefabInstanceComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& comp = registry.get<PrefabInstanceComponent>(entity);

    bool open = ImGui::CollapsingHeader("Prefab Instance##componentprefab", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteprefab");
    ImGui::PopStyleColor();

    if (open) {
        ImGui::TextDisabled("ID: %llu", comp.prefabId.id);
        if (ImGui::Checkbox("Master Prefab", &comp.bMasterPrefab)) {
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
    return {.requestRemoval = remove};
}

void NameComponent::Serialize(const NameComponent& comp, nlohmann::json& json)
{
    json["name"] = comp.name.c_str();
}

void NameComponent::Deserialize(NameComponent& comp, const nlohmann::json& json)
{
    comp.name = Core::InlineString<256>(json["name"].get<std::string_view>());
}

Engine::ComponentEditorResult NameComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<NameComponent>(entity);
    bool open = ImGui::CollapsingHeader("Name##componentname", ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        char buf[256];
        strncpy_s(buf, component.name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            component.name = Core::InlineString<256>(buf);
        }
    }
    return {};
}
} // Game::Component
