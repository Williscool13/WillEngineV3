
//
// Created by William on 2026-05-12.
//

#include "text_component.h"

#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "engine/core/registry.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/material_manager.h"
#include "engine/engine_api.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "engine/components/render_components.h"

namespace Engine::Component
{
void UnloadTextComponent(TextComponent& comp, Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& runtime = registry.GetOrAdd<TextRuntime>(entity);
    if (runtime.fontHandle.IsValid()) {
        registry.PushCommand({.type = CommandType::FontRelease, .payload = {.fontHandle = runtime.fontHandle}});
        runtime.fontHandle = {};
    }
    registry.Remove<TextFontRequest>(entity);
}

void LoadTextComponent(TextComponent& comp, Engine::SystemRegistry& registry, entt::entity entity)
{
    auto* state = registry.State();
    // Arm only; the load happens in ResolveTextFontPending (freeze-gated).
    auto& runtime = registry.GetOrAdd<TextRuntime>(entity);
    if (!runtime.modelRange.IsValid()) {
        runtime.modelRange = state->modelStore.Allocate(1);
    }
    if (comp.fontId.IsValid()) {
        registry.Set<TextFontRequest>(entity);
    }
}

void TextRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& runtime = registry.GetMut<TextRuntime>(entity);
    if (runtime.fontHandle.IsValid()) {
        registry.PushCommand({.type = CommandType::FontRelease, .payload = {.fontHandle = runtime.fontHandle}});
        runtime.fontHandle = {};
    }
    registry.PushCommand({.type = CommandType::MeshRelease, .payload = {.meshRelease = {0, 0, runtime.modelRange.offset, runtime.modelRange.count, StaticModelHandle::INVALID}}});
    runtime.modelRange = {};
}

void TextComponent::OnConstruct(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.PushCommand({.type = CommandType::TextConstruct, .entity = entity});

    auto* transform = registry.TryGet<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    registry.Set<RenderTransformRuntime>(entity, m, m);
    registry.Set<MultiframeDirtyRequest>(entity);
}

void TextComponent::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    registry.Remove<TextRuntime, TextFontRequest>(entity);
    registry.Remove<RenderTransformRuntime>(entity);
    registry.Remove<MultiframeDirtyRequest>(entity);
}


void TextComponent::OnEditCommit(Engine::SystemRegistry& registry, entt::entity entity)
{
    auto& comp = registry.GetMut<TextComponent>(entity);
    auto* ctx = registry.Context();
    const auto& runtime = registry.GetOrAdd<TextRuntime>(entity);
    const Engine::Font* font = runtime.fontHandle.IsValid() ? registry.Service<Engine::AssetManager>().GetFont(runtime.fontHandle) : nullptr;
    const bool bFontCurrent = (font != nullptr && font->fontId == comp.fontId) || registry.Has<TextFontRequest>(entity);
    if (!bFontCurrent) {
        UnloadTextComponent(comp, registry, entity);
        LoadTextComponent(comp, registry, entity);
    }
}

Engine::ComponentEditorResult TextComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    Engine::SystemRegistry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& comp = edit.Get<TextComponent>();

    bool open = ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletetext");
    ImGui::PopStyleColor();

    if (!open) {
        return {.bRequestRemoval = remove};
    }

    auto* ctx = registry.Context();
    auto& runtime = registry.GetOrAdd<TextRuntime>(entity);

    // Font picker
    const char* fontLabel = "(none)";
    const Engine::AssetManager::CachedFontMetadata* currentMeta = nullptr;
    if (runtime.fontHandle.IsValid()) {
        const Engine::Font* font = registry.Service<Engine::AssetManager>().GetFont(runtime.fontHandle);
        if (font) {
            currentMeta = registry.Service<Engine::AssetManager>().GetFontMetadata(font->fontId);
            if (currentMeta) { fontLabel = currentMeta->name.c_str(); }
        }
    }

    if (ImGui::BeginCombo("Font", fontLabel)) {
        const auto& fontCache = registry.Service<Engine::AssetManager>().GetFontCache();
        for (const auto& [fontId, meta] : fontCache) {
            bool selected = runtime.fontHandle.IsValid() &&
                            registry.Service<Engine::AssetManager>().GetFont(runtime.fontHandle) &&
                            registry.Service<Engine::AssetManager>().GetFont(runtime.fontHandle)->fontId == fontId;
            if (ImGui::Selectable(meta.name.c_str(), selected)) {
                edit.Set(&TextComponent::fontId, fontId);
            }
        }
        ImGui::EndCombo();
    }

    // TextMaterial picker
    {
        const char* matLabel = "(none)";
        const Engine::TextMaterial* currentMat = registry.Service<Engine::MaterialManager>().GetTextMaterial(comp.textMaterialId);
        if (currentMat) { matLabel = currentMat->name.c_str(); }

        if (ImGui::BeginCombo("Text Material", matLabel)) {
            if (ImGui::Selectable("(none)", !comp.textMaterialId.IsValid())) {
                edit.Set(&TextComponent::textMaterialId, Engine::TextMaterialID::INVALID);
            }
            const auto& textMats = registry.Service<Engine::MaterialManager>().GetTextMaterials();
            for (const auto& [matId, mat] : textMats) {
                bool selected = comp.textMaterialId == matId;
                if (ImGui::Selectable(mat.name.c_str(), selected)) {
                    edit.Set(&TextComponent::textMaterialId, matId);
                }
            }
            ImGui::EndCombo();
        }
    }

    EditWidgets::InputText(edit, "Text##field", &TextComponent::text);
    EditWidgets::DragFloat(edit, "Scale", &TextComponent::scale, 0.01f, 0.01f, 100.0f, "%.2f");
    EditWidgets::ColorEdit4(edit, "Color", &TextComponent::color);

    const char* alignLabels[] = {"Left", "Center", "Right"};
    EditWidgets::Combo(edit, "Align", &TextComponent::align, alignLabels, IM_ARRAYSIZE(alignLabels));

    const char* anchorLabels[] = {"Baseline", "Top", "Center", "Bottom"};
    EditWidgets::Combo(edit, "Anchor", &TextComponent::anchor, anchorLabels, IM_ARRAYSIZE(anchorLabels));

    EditWidgets::DragFloat(edit, "Wrap Width", &TextComponent::wrapWidth, 0.05f, 0.0f, 1000.0f, "%.2f");

    if (runtime.fontHandle.IsValid()) {
        const Engine::Font* font = registry.Service<Engine::AssetManager>().GetFont(runtime.fontHandle);
        if (font) {
            switch (font->loadState) {
                case Engine::Font::LoadState::Loading:      ImGui::TextDisabled("Loading..."); break;
                case Engine::Font::LoadState::FailedToLoad: ImGui::TextColored({1,0,0,1}, "Failed to load"); break;
                case Engine::Font::LoadState::Loaded:       ImGui::TextDisabled("Ready"); break;
                default: break;
            }
        }
    }

    return {.bRequestRemoval = remove};
}
} // Engine::Component
