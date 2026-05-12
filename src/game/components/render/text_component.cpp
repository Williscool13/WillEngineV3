
//
// Created by William on 2026-05-12.
//

#include "text_component.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"

namespace Game::Component
{
void TextComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& comp = registry.get<TextComponent>(entity);

    if (comp.fontId.IsValid() && !comp.fontHandle.IsValid()) {
        comp.fontHandle = ctx->assetManager->LoadFont(comp.fontId);
        registry.emplace_or_replace<TextLoadingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

void TextComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& comp = registry.get<TextComponent>(entity);
    if (comp.fontHandle.IsValid()) {
        ctx->assetManager->UnloadFont(comp.fontHandle);
    }
    registry.remove<TextLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<DirtyRenderTransformComponent>(entity);
}

void TextComponent::Serialize(const TextComponent& comp, nlohmann::json& json)
{
    json["fontId"] = comp.fontId.id;
    json["text"] = comp.text.c_str();
    json["renderSizePx"] = comp.renderSizePx;
    json["color"] = {comp.color.r, comp.color.g, comp.color.b, comp.color.a};
}

void TextComponent::Deserialize(TextComponent& comp, const nlohmann::json& json)
{
    comp.fontId = Engine::FontID(json["fontId"].get<uint64_t>());
    comp.text = Core::InlineString<256>(json["text"].get<std::string>().c_str());
    comp.renderSizePx = json["renderSizePx"].get<float>();
    if (json.contains("color")) {
        const auto& c = json["color"];
        comp.color = glm::vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
    }
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
        return {.requestRemoval = remove};
    }

    auto* ctx = registry.ctx().get<Engine::EngineContext*>();

    // Font picker
    const char* fontLabel = "(none)";
    const Engine::AssetManager::CachedFontMetadata* currentMeta = nullptr;
    if (comp.fontHandle.IsValid()) {
        Engine::Font* font = ctx->assetManager->GetFont(comp.fontHandle);
        if (font) {
            currentMeta = ctx->assetManager->GetFontMetadata(font->fontId);
            if (currentMeta) { fontLabel = currentMeta->name.c_str(); }
        }
    }

    if (ImGui::BeginCombo("Font", fontLabel)) {
        const auto& fontCache = ctx->assetManager->GetFontCache();
        for (const auto& [fontId, meta] : fontCache) {
            bool selected = comp.fontHandle.IsValid() &&
                            ctx->assetManager->GetFont(comp.fontHandle) &&
                            ctx->assetManager->GetFont(comp.fontHandle)->fontId == fontId;
            if (ImGui::Selectable(meta.name.c_str(), selected)) {
                if (comp.fontHandle.IsValid()) {
                    ctx->assetManager->UnloadFont(comp.fontHandle);
                }
                comp.fontId = fontId;
                comp.fontHandle = ctx->assetManager->LoadFont(fontId);
                registry.emplace_or_replace<TextLoadingTag>(entity);
            }
        }
        ImGui::EndCombo();
    }

    // Text content
    char buf[256];
    strncpy_s(buf, comp.text.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (ImGui::InputText("Text##field", buf, sizeof(buf))) {
        comp.text = Core::InlineString<256>(buf);
    }

    ImGui::DragFloat("Size (px)", &comp.renderSizePx, 1.0f, 1.0f, 2048.0f);
    ImGui::ColorEdit4("Color", glm::value_ptr(comp.color));

    if (comp.fontHandle.IsValid()) {
        Engine::Font* font = ctx->assetManager->GetFont(comp.fontHandle);
        if (font) {
            switch (font->loadState) {
                case Engine::Font::LoadState::Loading:     ImGui::TextDisabled("Loading..."); break;
                case Engine::Font::LoadState::FailedToLoad: ImGui::TextColored({1,0,0,1}, "Failed to load"); break;
                case Engine::Font::LoadState::Loaded:      ImGui::TextDisabled("Ready"); break;
                default: break;
            }
        }
    }

    return {.requestRemoval = remove};
}
} // Game::Component
