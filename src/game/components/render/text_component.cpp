
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
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"

namespace Game::Component
{
void UnloadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& runtime = registry.get_or_emplace<TextRuntime>(entity);
    if (runtime.fontHandle.IsValid()) {
        ctx->assetManager->UnloadFont(runtime.fontHandle);
        runtime.fontHandle = {};
    }
    registry.remove<TextFontPendingTag>(entity);
}

void LoadTextComponent(TextComponent& comp, entt::registry& registry, entt::entity entity)
{
    // Arm only; the load happens in ResolveTextFontPending (freeze-gated).
    registry.get_or_emplace<TextRuntime>(entity);
    if (comp.fontId.IsValid()) {
        registry.emplace_or_replace<TextFontPendingTag>(entity);
        registry.ctx().get<Engine::EngineState*>()->assetLoad.bPendingModelResolve = true;
    }
}

void TextComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<TextComponent>(entity);
    LoadTextComponent(comp, registry, entity);

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
}

void TextComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<TextComponent>(entity);
    UnloadTextComponent(comp, registry, entity);
    registry.remove<TextRuntime>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<MultiframeDirtyTransformComponent>(entity);
}

void TextComponent::Serialize(const TextComponent& comp, Engine::TextWriter& w)
{
    static const TextComponent DEF{};
    w.Key("fontId", comp.fontId.id);
    w.KeyOpt("textMaterialId", comp.textMaterialId.id, DEF.textMaterialId.id);
    if (!comp.text.IsEmpty()) {
        w.KeyStr("text", comp.text.View());
    }
    w.KeyOpt("renderSizePx", comp.renderSizePx, DEF.renderSizePx);
    w.KeyOpt("color", comp.color, DEF.color);
    w.KeyOpt("align", static_cast<uint32_t>(comp.align), static_cast<uint32_t>(DEF.align));
    w.KeyOpt("anchor", static_cast<uint32_t>(comp.anchor), static_cast<uint32_t>(DEF.anchor));
    w.KeyOpt("wrapWidthPx", comp.wrapWidthPx, DEF.wrapWidthPx);
}

void TextComponent::Deserialize(TextComponent& comp, const Engine::TextReader& r)
{
    comp.fontId = Engine::FontID(r.U64("fontId", comp.fontId.id));
    comp.textMaterialId = Engine::TextMaterialID(r.U64("textMaterialId", comp.textMaterialId.id));
    r.Str("text", comp.text);
    comp.renderSizePx = r.Float("renderSizePx", comp.renderSizePx);
    comp.color = r.Vec4("color", comp.color);
    comp.align = static_cast<Engine::Text3DAlign>(r.UInt("align", static_cast<uint32_t>(comp.align)));
    comp.anchor = static_cast<Engine::Text3DAnchor>(r.UInt("anchor", static_cast<uint32_t>(comp.anchor)));
    comp.wrapWidthPx = r.Float("wrapWidthPx", comp.wrapWidthPx);
}

Engine::ComponentEditorResult TextComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& comp = registry.get<TextComponent>(entity);

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
    bool modified = false;

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
                UnloadTextComponent(comp, registry, entity);
                comp.fontId = fontId;
                LoadTextComponent(comp, registry, entity);
                modified = true;
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
                comp.textMaterialId = Engine::TextMaterialID::INVALID;
                modified = true;
            }
            const auto& textMats = ctx->materialManager->GetTextMaterials();
            for (const auto& [matId, mat] : textMats) {
                bool selected = comp.textMaterialId == matId;
                if (ImGui::Selectable(mat.name.c_str(), selected)) {
                    comp.textMaterialId = matId;
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    // Text content
    char buf[256];
    strncpy_s(buf, comp.text.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Text##field", buf, sizeof(buf))) {
        comp.text = Core::InlineString<256>(buf);
        modified = true;
    }

    modified |= ImGui::DragFloat("Size (px)", &comp.renderSizePx, 1.0f, 1.0f, 2048.0f);
    modified |= ImGui::ColorEdit4("Color", glm::value_ptr(comp.color));

    const char* alignLabels[] = {"Left", "Center", "Right"};
    int alignIdx = static_cast<int>(comp.align);
    if (ImGui::Combo("Align", &alignIdx, alignLabels, IM_ARRAYSIZE(alignLabels))) {
        comp.align = static_cast<Engine::Text3DAlign>(alignIdx);
        modified = true;
    }

    const char* anchorLabels[] = {"Baseline", "Top", "Center", "Bottom"};
    int anchorIdx = static_cast<int>(comp.anchor);
    if (ImGui::Combo("Anchor", &anchorIdx, anchorLabels, IM_ARRAYSIZE(anchorLabels))) {
        comp.anchor = static_cast<Engine::Text3DAnchor>(anchorIdx);
        modified = true;
    }

    modified |= ImGui::DragFloat("Wrap Width (px)", &comp.wrapWidthPx, 1.0f, 0.0f, 8192.0f);

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

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game::Component
