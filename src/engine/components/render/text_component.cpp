
//
// Created by William on 2026-05-12.
//

#include "text_component.h"

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/material_manager.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "engine/components/render_components.h"

namespace Engine::Component
{
void UnloadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get_or_emplace<TextRuntime>(entity);
    if (runtime.fontHandle.IsValid()) {
        state->commandQueue.Push({.type = CommandType::FontRelease, .payload = {.fontHandle = runtime.fontHandle}});
        runtime.fontHandle = {};
    }
    registry.remove<TextFontPendingTag>(entity);
}

void LoadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    // Arm only; the load happens in ResolveTextFontPending (freeze-gated).
    auto& runtime = registry.get_or_emplace<TextRuntime>(entity);
    if (!runtime.modelRange.IsValid()) {
        runtime.modelRange = state->modelStore.Allocate(1);
    }
    if (comp.fontId.IsValid()) {
        registry.emplace_or_replace<TextFontPendingTag>(entity);
    }
}

void TextRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get<TextRuntime>(entity);
    state->commandQueue.Push({.type = CommandType::MeshRelease, .payload = {.meshRelease = {0, 0, runtime.modelRange.offset, runtime.modelRange.count, StaticModelHandle::INVALID}}});
    runtime.modelRange = {};
}

void TextComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::TextConstruct, .entity = entity});

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void TextComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<TextComponent>(entity);
    UnloadTextComponent(comp, registry, entity);
    registry.remove<TextRuntime>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<MultiframeDirtyComponent>(entity);
}


void TextComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<TextComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    const auto& runtime = registry.get_or_emplace<TextRuntime>(entity);
    const Engine::Font* font = runtime.fontHandle.IsValid() ? ctx->assetManager->GetFont(runtime.fontHandle) : nullptr;
    const bool bFontCurrent = (font != nullptr && font->fontId == comp.fontId) || registry.all_of<TextFontPendingTag>(entity);
    if (!bFontCurrent) {
        UnloadTextComponent(comp, registry, entity);
        LoadTextComponent(comp, registry, entity);
    }
}

Engine::ComponentEditorResult TextComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
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

    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& runtime = registry.get_or_emplace<TextRuntime>(entity);

    // Font picker
    const char* fontLabel = "(none)";
    const Engine::AssetManager::CachedFontMetadata* currentMeta = nullptr;
    if (runtime.fontHandle.IsValid()) {
        Engine::Font* font = ctx->assetManager->GetFont(runtime.fontHandle);
        if (font) {
            currentMeta = ctx->assetManager->GetFontMetadata(font->fontId);
            if (currentMeta) { fontLabel = currentMeta->name.c_str(); }
        }
    }

    if (ImGui::BeginCombo("Font", fontLabel)) {
        const auto& fontCache = ctx->assetManager->GetFontCache();
        for (const auto& [fontId, meta] : fontCache) {
            bool selected = runtime.fontHandle.IsValid() &&
                            ctx->assetManager->GetFont(runtime.fontHandle) &&
                            ctx->assetManager->GetFont(runtime.fontHandle)->fontId == fontId;
            if (ImGui::Selectable(meta.name.c_str(), selected)) {
                edit.Set(&TextComponent::fontId, fontId);
            }
        }
        ImGui::EndCombo();
    }

    // TextMaterial picker
    {
        const char* matLabel = "(none)";
        const Engine::TextMaterial* currentMat = ctx->materialManager->GetTextMaterial(comp.textMaterialId);
        if (currentMat) { matLabel = currentMat->name.c_str(); }

        if (ImGui::BeginCombo("Text Material", matLabel)) {
            if (ImGui::Selectable("(none)", !comp.textMaterialId.IsValid())) {
                edit.Set(&TextComponent::textMaterialId, Engine::TextMaterialID::INVALID);
            }
            const auto& textMats = ctx->materialManager->GetTextMaterials();
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
        Engine::Font* font = ctx->assetManager->GetFont(runtime.fontHandle);
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
