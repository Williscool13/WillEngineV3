//
// Created by William on 2026-07-22.
//

#include "reflection_probe_component.h"

#include <imgui.h>

#include "game/component-registry/component_editor.h"
#include "game/component-registry/json_helpers.h"
#include "game/components/core_components.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"

namespace Game::Component
{
static void RequestReflectionProbeLoad(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.remove<ReflectionProbeLoadingTag>(entity);
    if (comp.standInEnvMap.IsValid()) {
        registry.emplace_or_replace<ReflectionProbeLoadPendingTag>(entity);
        state->bPendingModelResolve |= true;
    }
}

Engine::ComponentEditorResult ReflectionProbeComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletereflectionprobe");
    ImGui::PopStyleColor();

    if (open) {
        auto& comp = registry.get<ReflectionProbeComponent>(entity);

        static constexpr const char* SHAPE_LABELS[] = {"Box", "Sphere"};
        int shapeIndex = static_cast<int>(comp.shape);
        if (ImGui::Combo("Shape##rp", &shapeIndex, SHAPE_LABELS, 2)) {
            comp.shape = static_cast<Shape>(shapeIndex);
        }

        ImGui::DragFloat("Fade Margin##rp", &comp.fadeMargin, 0.02f, 0.0f, 10.0f);
        ImGui::DragFloat3("Capture Offset##rp", &comp.captureOffset.x, 0.05f);
        ImGui::Checkbox("Parallax##rp", &comp.bParallax);

        static constexpr const char* RESOLUTION_LABELS[] = {"128", "256"};
        int resolutionIndex = static_cast<int>(comp.resolution);
        if (ImGui::Combo("Resolution##rp", &resolutionIndex, RESOLUTION_LABELS, 2)) {
            comp.resolution = static_cast<Resolution>(resolutionIndex);
        }

        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const Engine::AssetManager::CachedCubemapMetadata* currentMeta = ctx->assetManager->GetCubemapMetadata(comp.standInEnvMap);
        const char* preview = currentMeta ? currentMeta->name.c_str() : "None";
        if (ImGui::BeginCombo("Stand-in Env Map##rp", preview)) {
            for (const auto& [id, meta] : ctx->assetManager->GetCubemapCache()) {
                const bool selected = id == comp.standInEnvMap;
                if (ImGui::Selectable(meta.name.c_str(), selected) && id != comp.standInEnvMap) {
                    if (comp.contentHandle.IsValid()) {
                        ctx->assetManager->UnloadCubemap(comp.contentHandle);
                        comp.contentHandle = Engine::CubemapHandle::INVALID;
                    }
                    comp.standInEnvMap = id;
                    RequestReflectionProbeLoad(registry, entity);
                }
            }
            ImGui::EndCombo();
        }
    }

    return {.requestRemoval = remove};
}

void ReflectionProbeComponent::Serialize(const ReflectionProbeComponent& comp, nlohmann::json& json)
{
    json["probeId"] = comp.probeId;
    json["shape"] = static_cast<uint32_t>(comp.shape);
    json["fadeMargin"] = comp.fadeMargin;
    json["captureOffset"] = {comp.captureOffset.x, comp.captureOffset.y, comp.captureOffset.z};
    json["bParallax"] = comp.bParallax;
    json["resolution"] = static_cast<uint32_t>(comp.resolution);
    json["standInEnvMap"] = comp.standInEnvMap.id;
}

void ReflectionProbeComponent::Deserialize(ReflectionProbeComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.probeId = json.value("probeId", uint64_t{0});
    comp.shape = static_cast<Shape>(json.value("shape", static_cast<uint32_t>(Shape::Box)));
    comp.fadeMargin = json.value("fadeMargin", 0.5f);
    comp.captureOffset = json.contains("captureOffset") ? json["captureOffset"].get<Vec3>() : Vec3{0.0f, 0.0f, 0.0f};
    comp.bParallax = json.value("bParallax", true);
    comp.resolution = static_cast<Resolution>(json.value("resolution", static_cast<uint32_t>(Resolution::Res256)));
    comp.standInEnvMap = Engine::EnvironmentMapID{json.value("standInEnvMap", uint64_t{0})};
}

void ReflectionProbeComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();
    if (comp.probeId == 0) {
        comp.probeId = state->rng();
    }
    RequestReflectionProbeLoad(registry, entity);
}

void ReflectionProbeComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<ReflectionProbeComponent>(entity);
    if (comp.contentHandle.IsValid()) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        ctx->assetManager->UnloadCubemap(comp.contentHandle);
        comp.contentHandle = Engine::CubemapHandle::INVALID;
    }
    registry.remove<ReflectionProbeLoadPendingTag>(entity);
    registry.remove<ReflectionProbeLoadingTag>(entity);
}
}
