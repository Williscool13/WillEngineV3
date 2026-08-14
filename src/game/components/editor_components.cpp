//
// Created by William on 2026-03-16.
//

#include "editor_components.h"

#include <fmt/format.h>

#include "component_types.h"
#include "core/containers/arena_vector.h"
#include "engine/include/engine_context.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void EntityFolderComponent::Serialize(const EntityFolderComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("folderId", comp.folderId.id, uint64_t{0});
}

void EntityFolderComponent::Deserialize(EntityFolderComponent& comp, const Engine::TextReader& r)
{
    comp.folderId = StringID(r.U64("folderId", comp.folderId.id));
}

Engine::ComponentEditorResult EntityFolderComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                        const char* name)
{
    auto& comp = registry.get<EntityFolderComponent>(entity);
    bool open = ImGui::CollapsingHeader("Folder##entityfolder", ImGuiTreeNodeFlags_DefaultOpen);

    bool modified = false;
    if (open) {
        const char* current = "(None)";
        auto anchorView = registry.view<SceneFolderComponent>();
        for (auto a : anchorView) {
            if (anchorView.get<SceneFolderComponent>(a).folderId == comp.folderId) {
                current = anchorView.get<SceneFolderComponent>(a).name.c_str();
                break;
            }
        }

        ImGui::Text("Folder");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##entity_folder", comp.folderId.IsValid() ? current : "(None)")) {
            if (ImGui::Selectable("(None)", !comp.folderId.IsValid())) {
                comp.folderId = StringID();
                modified = true;
            }
            for (auto a : anchorView) {
                const auto& fc = anchorView.get<SceneFolderComponent>(a);
                Core::ShortString label;
                if (fc.parentFolder.IsValid()) { label.Append("    "); }
                label.Append(fc.name);
                if (ImGui::Selectable(label.c_str(), comp.folderId == fc.folderId)) {
                    comp.folderId = fc.folderId;
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    return {.bModified = modified};
}

bool SceneFolderComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return false;
}

void SceneFolderComponent::Serialize(const SceneFolderComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("folderId", comp.folderId.id, uint64_t{0});
    w.KeyOpt("parentFolder", comp.parentFolder.id, uint64_t{0});
    if (!comp.name.IsEmpty()) {
        w.KeyStr("name", comp.name.View());
    }
}

void SceneFolderComponent::Deserialize(SceneFolderComponent& comp, const Engine::TextReader& r)
{
    comp.folderId = StringID(r.U64("folderId", comp.folderId.id));
    comp.parentFolder = StringID(r.U64("parentFolder", comp.parentFolder.id));
    r.Str("name", comp.name);
}
} // Game::Component
