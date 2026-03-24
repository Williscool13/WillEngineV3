//
// Created by William on 2026-03-16.
//

#include "editor_components.h"

#include <vector>

#include <fmt/format.h>
#include <json/nlohmann/json.hpp>

#include "component_types.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void EntityFolderComponent::Serialize(const EntityFolderComponent& comp, nlohmann::json& json)
{
    json["folderHierarchyNames"] = nlohmann::json::array();
    for (const ShortString& name : comp.folderHierarchyNames) {
        json["folderHierarchyNames"].push_back(name.c_str());
    }
}

void EntityFolderComponent::Deserialize(EntityFolderComponent& comp, const nlohmann::json& json)
{
    if (json.contains("folderHierarchyNames")) {
        const auto& arr = json["folderHierarchyNames"];
        for (size_t i = 0; i < comp.folderHierarchyNames.size() && i < arr.size(); ++i) {
            const std::string s = arr[i].get<std::string>();
            comp.folderHierarchyNames[i] = ShortString(s.c_str());
            comp.folderHierarchy[i] = s.empty() ? StringID() : StringID(s.c_str(), s.size());
        }
    }
}

static void CollectExistingFolderNames(entt::registry& registry, int level,
                                       std::vector<ShortString>& outNames, StringID parentFilter)
{
    auto view = registry.view<EntityFolderComponent>();
    for (auto e : view) {
        auto& fc = view.get<EntityFolderComponent>(e);
        if (fc.folderHierarchyNames[level].size() == 0) continue;
        if (level == 1 && fc.folderHierarchy[0] != parentFilter) continue;

        bool duplicate = false;
        for (auto& existing : outNames) {
            if (existing == fc.folderHierarchyNames[level]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            outNames.push_back(fc.folderHierarchyNames[level]);
        }
    }
    std::ranges::sort(outNames);
}

static bool DrawFolderLevelCombo(const char* label, entt::registry& registry, int level,
                                 ShortString& nameOut, StringID& idOut, StringID parentFilter)
{
    bool changed = false;
    std::vector<ShortString> existing;
    CollectExistingFolderNames(registry, level, existing, parentFilter);

    const char* currentName = nameOut.size() > 0 ? nameOut.c_str() : "(None)";
    if (ImGui::BeginCombo(label, currentName)) {
        if (ImGui::Selectable("(None)", nameOut.size() == 0)) {
            nameOut = ShortString();
            idOut = StringID();
            changed = true;
        }
        for (auto& folderName : existing) {
            bool selected = folderName == nameOut;
            if (ImGui::Selectable(folderName.c_str(), selected)) {
                nameOut = folderName;
                idOut = StringID(folderName.c_str(), folderName.size());
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    char newBuf[16] = {};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint(fmt::format("##new_{}", label).c_str(), "New folder name...",
                                 newBuf, sizeof(newBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (newBuf[0]) {
            nameOut = ShortString(newBuf);
            idOut = StringID(newBuf, strlen(newBuf));
            changed = true;
        }
    }
    return changed;
}

ComponentEditorResult EntityFolderComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                        const char* name)
{
    auto& comp = registry.get<EntityFolderComponent>(entity);
    bool open = ImGui::CollapsingHeader("Folder##entityfolder", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletefolder");
    ImGui::PopStyleColor();

    if (open) {
        ImGui::Text("Folder");
        DrawFolderLevelCombo("##folder_0", registry, 0, comp.folderHierarchyNames[0], comp.folderHierarchy[0], StringID());

        if (comp.folderHierarchy[0].IsValid()) {
            ImGui::Text("Subfolder");
            DrawFolderLevelCombo("##folder_1", registry, 1, comp.folderHierarchyNames[1], comp.folderHierarchy[1], comp.folderHierarchy[0]);
        }
        else {
            comp.folderHierarchyNames[1] = ShortString();
            comp.folderHierarchy[1] = StringID();
        }
    }
    return {.requestRemoval = remove};
}
} // Game::Component
