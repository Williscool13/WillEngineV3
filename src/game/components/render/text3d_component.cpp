//
// Created by William on 2026-06-22.
//

#include "text3d_component.h"

#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"

#include "static_mesh_component.h"
#include "spline_mesh_component.h"
#include "procedural_mesh_component.h"
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
void UnloadText3DFont(entt::registry& registry, entt::entity entity)
{
    registry.remove<Component::MeshRuntime>(entity);
    registry.remove<Text3DGeneratePendingTag>(entity);
    registry.remove<Text3DLoadingTag>(entity);
}

void LoadText3DFont(Text3DComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.remove<Text3DLoadingTag>(entity);
    if (component.fontId.IsValid()) {
        registry.emplace_or_replace<Text3DGeneratePendingTag>(entity);
        state->assetLoad.bPendingModelResolve = true;
    }
    else {
        registry.remove<Text3DGeneratePendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
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
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
}

void Text3DComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    UnloadText3DFont(registry, entity);
    registry.remove<RenderTransformComponent>(entity);
}
}

namespace Game
{
bool Component::Text3DComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::SplineMeshComponent, Component::ProceduralMeshComponent>(entity);
}

void Component::Text3DComponent::Serialize(const Text3DComponent& comp, Engine::TextWriter& w)
{
    static const Text3DComponent DEF{};
    w.Key("fontId", comp.fontId.id);
    if (!comp.text.IsEmpty()) {
        w.KeyStr("text", comp.text.View());
    }
    w.KeyOpt("depth", comp.depth, DEF.depth);
    w.KeyOpt("flatness", comp.flatness, DEF.flatness);
    w.KeyOpt("tracking", comp.tracking, DEF.tracking);
    w.KeyOpt("scale", comp.scale, DEF.scale);
    w.KeyOpt("wrapWidth", comp.wrapWidth, DEF.wrapWidth);
    w.KeyOpt("bendRadius", comp.bendRadius, DEF.bendRadius);
    w.KeyOpt("smoothNormals", comp.bSmoothNormals, DEF.bSmoothNormals);
    w.KeyOpt("align", static_cast<uint32_t>(comp.align), static_cast<uint32_t>(DEF.align));
    w.KeyOpt("anchor", static_cast<uint32_t>(comp.anchor), static_cast<uint32_t>(DEF.anchor));
    w.Key("material", comp.material.id);
    w.KeyOpt("renderOffset", comp.renderOffset, DEF.renderOffset);
    w.KeyOpt("renderRotation", comp.renderRotation, DEF.renderRotation);
}

void Component::Text3DComponent::Deserialize(Text3DComponent& comp, const Engine::TextReader& r)
{
    comp.fontId = Engine::FontID(r.U64("fontId", comp.fontId.id));
    r.Str("text", comp.text);
    comp.depth = r.Float("depth", comp.depth);
    comp.flatness = r.Float("flatness", comp.flatness);
    comp.tracking = r.Float("tracking", comp.tracking);
    comp.scale = r.Float("scale", comp.scale);
    comp.wrapWidth = r.Float("wrapWidth", comp.wrapWidth);
    comp.bendRadius = r.Float("bendRadius", comp.bendRadius);
    comp.bSmoothNormals = r.Bool("smoothNormals", comp.bSmoothNormals);
    comp.align = static_cast<Engine::Text3DAlign>(r.UInt("align", static_cast<uint32_t>(comp.align)));
    comp.anchor = static_cast<Engine::Text3DAnchor>(r.UInt("anchor", static_cast<uint32_t>(comp.anchor)));
    comp.material = Engine::MaterialID(r.U64("material", comp.material.id));
    comp.renderOffset = r.Vec3("renderOffset", comp.renderOffset);
    comp.renderRotation = r.Quat("renderRotation", comp.renderRotation);
}

Engine::ComponentEditorResult Component::Text3DComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& comp = registry.get<Text3DComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
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

    auto& renderFlags = registry.get_or_emplace<RenderFlagsComponent>(entity);
    bool visible = renderFlags.Has(RenderFlagsComponent::VISIBLE);
    bool ddgiContribution = renderFlags.Has(RenderFlagsComponent::DDGI_CONTRIBUTE);
    if (ImGui::Checkbox("Visible##text3d", &visible)) { renderFlags.Set(RenderFlagsComponent::VISIBLE, visible); }
    ImGui::SameLine();
    if (ImGui::Checkbox("DDGI Contribution##text3d", &ddgiContribution)) { renderFlags.Set(RenderFlagsComponent::DDGI_CONTRIBUTE, ddgiContribution); }
    bool probeBakeExclude = !renderFlags.Has(RenderFlagsComponent::PROBE_BAKE_INCLUDE);
    if (ImGui::Checkbox("Probe Bake Exclude##text3d", &probeBakeExclude)) { renderFlags.Set(RenderFlagsComponent::PROBE_BAKE_INCLUDE, !probeBakeExclude); }
    ImGui::SameLine();
    bool motionBlurExclude = !renderFlags.Has(RenderFlagsComponent::MOTION_BLUR);
    if (ImGui::Checkbox("Motion Blur Exclude##text3d", &motionBlurExclude)) { renderFlags.Set(RenderFlagsComponent::MOTION_BLUR, !motionBlurExclude); }

    bool modified = false;
    ImGui::BeginDisabled(busy);

    const char* fontLabel = "(none)";
    if (const Engine::AssetManager::CachedFontMetadata* meta = ctx->assetManager->GetFontMetadata(comp.fontId)) {
        fontLabel = meta->name.c_str();
    }
    if (ImGui::BeginCombo("Font", fontLabel)) {
        const auto& fontCache = ctx->assetManager->GetFontCache();
        for (const auto& [fontId, meta] : fontCache) {
            const bool selected = comp.fontId == fontId;
            if (ImGui::Selectable(meta.name.c_str(), selected) && fontId != comp.fontId) {
                Component::UnloadText3DFont(registry, entity);
                comp.fontId = fontId;
                LoadText3DFont(comp, registry, entity);
                modified = true;
            }
        }
        ImGui::EndCombo();
    }

    char buf[256];
    strncpy_s(buf, comp.text.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    bool dirty = false;
    ImGui::InputTextMultiline("Text##text3dfield", buf, sizeof(buf), ImVec2(0.0f, ImGui::GetTextLineHeight() * 4.0f));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        comp.text = Core::InlineString<256>(buf);
        dirty = true;
    }

    ImGui::DragFloat("Depth", &comp.depth, 0.005f, 0.001f, 10.0f, "%.3f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat("Scale", &comp.scale, 0.01f, 0.01f, 100.0f, "%.3f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat("Tracking", &comp.tracking, 0.005f, -1.0f, 1.0f, "%.3f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat("Wrap Width", &comp.wrapWidth, 0.05f, 0.0f, 1000.0f, "%.2f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat("Bend Radius", &comp.bendRadius, 0.05f, -1000.0f, 1000.0f, "%.2f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();

    const char* alignLabels[] = {"Left", "Center", "Right"};
    int alignIdx = static_cast<int>(comp.align);
    if (ImGui::Combo("Align", &alignIdx, alignLabels, IM_ARRAYSIZE(alignLabels))) {
        comp.align = static_cast<Engine::Text3DAlign>(alignIdx);
        dirty = true;
    }

    const char* anchorLabels[] = {"Baseline", "Top", "Center", "Bottom"};
    int anchorIdx = static_cast<int>(comp.anchor);
    if (ImGui::Combo("Anchor", &anchorIdx, anchorLabels, IM_ARRAYSIZE(anchorLabels))) {
        comp.anchor = static_cast<Engine::Text3DAnchor>(anchorIdx);
        dirty = true;
    }

    ImGui::DragFloat("Flatness", &comp.flatness, 0.0005f, 0.0005f, 0.1f, "%.4f");
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
    dirty |= ImGui::Checkbox("Smooth Normals", &comp.bSmoothNormals);

    if (dirty) {
        modified = true;
        registry.emplace_or_replace<Text3DGeneratePendingTag>(entity);
        state->assetLoad.bPendingModelResolve |= true;
    }

    ImGui::EndDisabled();

    // Material change only re-binds at resolve; no regenerate needed.
    {
        const char* currentLabel = "(none)";
        if (comp.material.IsValid()) {
            if (const Engine::Material* m = ctx->materialManager->GetMaterial(comp.material)) {
                currentLabel = m->name.c_str();
            }
        }
        if (ImGui::BeginCombo("Material", currentLabel)) {
            if (ImGui::Selectable("(none)", !comp.material.IsValid()) && comp.material.IsValid()) {
                comp.material = Engine::MaterialID{};
                registry.emplace_or_replace<Text3DLoadingTag>(entity);
                state->assetLoad.bPendingModelResolve |= true;
                modified = true;
            }
            for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                if (mat.immutable) { continue; }
                if (ImGui::Selectable(mat.name.c_str(), matId == comp.material) && matId != comp.material) {
                    comp.material = matId;
                    registry.emplace_or_replace<Text3DLoadingTag>(entity);
                    state->assetLoad.bPendingModelResolve |= true;
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SeparatorText("Render Transform");
    if (ImGui::DragFloat3("Offset", glm::value_ptr(comp.renderOffset), 0.01f)) {
        modified = true;
        if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
            rt->renderOffset = comp.renderOffset;
            registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
        }
    }
    glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(comp.renderRotation));
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(renderEuler), 0.5f)) {
        modified = true;
        comp.renderRotation = glm::quat(glm::radians(renderEuler));
        if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
            rt->renderRotation = comp.renderRotation;
            registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
        }
    }

    if (const Engine::AssetManager::CachedFontMetadata* meta = ctx->assetManager->GetFontMetadata(comp.fontId)) {
        if (meta->header.contourGlyphCount == 0) {
            ImGui::TextColored({1, 0.6f, 0, 1}, "Font has no contours; regenerate it with the updated importer.");
        }
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}
}
