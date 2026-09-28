//
// Created by William on 2026-06-22.
//

#include "text3d_component.h"

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "mesh_source_exclusion.h"
#include "static_mesh_component.h"
#include "spline_mesh_component.h"
#include "procedural_mesh_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/material_manager.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/editor/editor_materials.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "engine/components/render_components.h"

namespace Engine::Component
{
void UnloadText3DFont(entt::registry& registry, entt::entity entity)
{
    registry.remove<Component::MeshRuntime>(entity);
    registry.remove<Text3DGeneratePendingTag>(entity);
    registry.remove<Text3DLoadingTag>(entity);
}

void LoadText3DFont(Text3DComponent& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<Text3DLoadingTag>(entity);
    if (component.fontId.IsValid()) {
        registry.emplace_or_replace<Text3DGeneratePendingTag>(entity);
    }
    else {
        registry.remove<Text3DGeneratePendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void Text3DComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<Text3DComponent>(entity);
    LoadText3DFont(component, registry, entity);

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void Text3DComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    UnloadText3DFont(registry, entity);
    registry.remove<RenderTransformComponent>(entity);
}
}

namespace Engine
{
bool Component::Text3DComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return Component::MeshSources::NoneOtherThan<Component::Text3DComponent>(registry, entity);
}


void Component::Text3DComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    const auto& comp = registry.get<Text3DComponent>(entity);
    if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
        rt->renderOffset = comp.renderOffset;
        rt->renderRotation = comp.renderRotation;
        registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    }
}

void Component::Text3DComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<Text3DComponent>(entity);
    if (!comp.fontId.IsValid()) {
        UnloadText3DFont(registry, entity);
        return;
    }
    registry.emplace_or_replace<Text3DGeneratePendingTag>(entity);
    OnEditPreview(registry, entity);
}

Engine::ComponentEditorResult Component::Text3DComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& comp = edit.Get<Text3DComponent>();
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = edit.State();
    const bool busy = registry.any_of<Text3DGeneratePendingTag, Text3DLoadingTag>(entity);

    bool open = ImGui::CollapsingHeader("3D Text", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletetext3d");
    ImGui::PopStyleColor();

    if (!open) {
        return {.bRequestRemoval = remove};
    }

    if (busy) {
        ImGui::TextDisabled("Generating mesh...");
    }

    if (DrawRenderFlagToggles(edit, RENDER_TOGGLE_ALL & ~RENDER_TOGGLE_ALPHA_CUTOUT)) {
        edit.ForEachTarget<Text3DComponent>([&registry](entt::entity e) { registry.emplace_or_replace<Text3DGeneratePendingTag>(e); });
    }

    ImGui::BeginDisabled(busy);

    const char* fontLabel = "(none)";
    if (const Engine::AssetManager::CachedFontMetadata* meta = ctx->assetManager->GetFontMetadata(comp.fontId)) {
        fontLabel = meta->name.c_str();
    }
    if (ImGui::BeginCombo("Font", edit.IsMixed(&Text3DComponent::fontId) ? "--" : fontLabel)) {
        const auto& fontCache = ctx->assetManager->GetFontCache();
        for (const auto& [fontId, meta] : fontCache) {
            if (ImGui::Selectable(meta.name.c_str(), comp.fontId == fontId)) {
                edit.Set(&Text3DComponent::fontId, fontId);
            }
        }
        ImGui::EndCombo();
    }

    // Geometry fields regenerate the mesh, so they commit on release only
    char buf[256];
    strncpy_s(buf, comp.text.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    ImGui::InputTextMultiline("Text##text3dfield", buf, sizeof(buf), ImVec2(0.0f, ImGui::GetTextLineHeight() * 4.0f));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        edit.Set(&Text3DComponent::text, Core::InlineString<256>(buf));
    }

    EditWidgets::DragFloat(edit, "Depth", &Text3DComponent::depth, 0.005f, 0.001f, 10.0f, "%.3f");
    EditWidgets::DragFloat(edit, "Scale", &Text3DComponent::scale, 0.01f, 0.01f, 100.0f, "%.3f");
    EditWidgets::DragFloat(edit, "Tracking", &Text3DComponent::tracking, 0.005f, -1.0f, 1.0f, "%.3f");
    EditWidgets::DragFloat(edit, "Wrap Width", &Text3DComponent::wrapWidth, 0.05f, 0.0f, 1000.0f, "%.2f");
    EditWidgets::DragFloat(edit, "Bend Radius", &Text3DComponent::bendRadius, 0.05f, -1000.0f, 1000.0f, "%.2f");

    const char* alignLabels[] = {"Left", "Center", "Right"};
    EditWidgets::Combo(edit, "Align", &Text3DComponent::align, alignLabels, IM_ARRAYSIZE(alignLabels));

    const char* anchorLabels[] = {"Baseline", "Top", "Center", "Bottom"};
    EditWidgets::Combo(edit, "Anchor", &Text3DComponent::anchor, anchorLabels, IM_ARRAYSIZE(anchorLabels));

    EditWidgets::DragFloat(edit, "Flatness", &Text3DComponent::flatness, 0.0005f, 0.0005f, 0.1f, "%.4f");
    EditWidgets::Checkbox(edit, "Smooth Normals", &Text3DComponent::bSmoothNormals);

    ImGui::EndDisabled();

    {
        const char* currentLabel = "(none)";
        if (comp.material.IsValid()) {
            if (const Engine::Material* m = ctx->materialManager->GetMaterial(comp.material)) {
                currentLabel = m->name.c_str();
            }
        }
        if (ImGui::BeginCombo("Material", edit.IsMixed(&Text3DComponent::material) ? "--" : currentLabel, ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("(none)", !comp.material.IsValid())) {
                edit.Set(&Text3DComponent::material, Engine::MaterialID{});
            }
            const Engine::MaterialID picked = Engine::DrawMaterialSelector(ctx, state, state->editor.materialSelector, comp.material);
            if (picked.IsValid() && picked != comp.material) {
                edit.Set(&Text3DComponent::material, picked);
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SeparatorText("Render Transform");
    EditWidgets::DragFloat3(edit, "Offset", &Text3DComponent::renderOffset, 0.01f);
    glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(comp.renderRotation));
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(renderEuler), 0.5f)) {
        edit.PreviewSet(&Text3DComponent::renderRotation, glm::quat(glm::radians(renderEuler)));
    }
    EditWidgets::CommitOnRelease<Text3DComponent>(edit, false);

    if (const Engine::AssetManager::CachedFontMetadata* meta = ctx->assetManager->GetFontMetadata(comp.fontId)) {
        if (meta->header.contourGlyphCount == 0) {
            ImGui::TextColored({1, 0.6f, 0, 1}, "Font has no contours; regenerate it with the updated importer.");
        }
    }

    return {.bRequestRemoval = remove};
}
}
