//
// Created by William on 2026-07-02.
//

#include "static_mesh_primitive_component.h"

#include <entt/entt.hpp>

#include "imgui.h"
#include <glm/gtc/quaternion.hpp>

#include "mesh_source_exclusion.h"
#include "core/containers/arena_array.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/editor/editor_materials.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "engine/components/render/static_mesh_component.h"

namespace Engine::Component
{
void UnloadStaticMeshPrimitive(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<StaticMeshPrimitiveLoadPendingTag>(entity);
    registry.remove<StaticMeshPrimitiveLoadingTag>(entity);
}

void LoadStaticMeshPrimitive(StaticMeshPrimitiveComponent& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<StaticMeshPrimitiveLoadingTag>(entity);
    if (component.modelId.IsValid() && component.primitiveOrdinal != ~0u) {
        registry.emplace_or_replace<StaticMeshPrimitiveLoadPendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}

void StaticMeshPrimitiveComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<StaticMeshPrimitiveComponent>(entity);
    if (!component.modelId.IsValid() || component.primitiveOrdinal == ~0u) {
        return;
    }
    LoadStaticMeshPrimitive(component, registry, entity);
}

void StaticMeshPrimitiveComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    UnloadStaticMeshPrimitive(registry, entity);
    registry.remove<RenderTransformComponent>(entity);
}

bool StaticMeshPrimitiveComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return MeshSources::NoneOtherThan<StaticMeshPrimitiveComponent>(registry, entity);
}


void StaticMeshPrimitiveComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    const auto& component = registry.get<StaticMeshPrimitiveComponent>(entity);
    if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
        rt->renderOffset = component.renderOffset;
        rt->renderRotation = component.renderRotation;
        registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    }
}

void StaticMeshPrimitiveComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<StaticMeshPrimitiveComponent>(entity);
    if (!component.modelId.IsValid()) {
        UnloadStaticMeshPrimitive(registry, entity);
        registry.remove<RenderTransformComponent>(entity);
        registry.remove<MultiframeDirtyComponent>(entity);
        return;
    }
    if (!registry.all_of<RenderTransformComponent>(entity)) {
        LoadStaticMeshPrimitive(component, registry, entity);
        return;
    }
    if (component.primitiveOrdinal != ~0u) {
        registry.emplace_or_replace<StaticMeshPrimitiveLoadingTag>(entity);
    }
    OnEditPreview(registry, entity);
}

Engine::ComponentEditorResult StaticMeshPrimitiveComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& component = edit.Get<StaticMeshPrimitiveComponent>();
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = edit.State();

    bool open = ImGui::CollapsingHeader("Static Mesh Primitive", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestaticmeshprimitive");
    ImGui::PopStyleColor();

    if (open) {
        if (DrawRenderFlagToggles(edit, RENDER_TOGGLE_VISIBLE | RENDER_TOGGLE_PROBE_BAKE | RENDER_TOGGLE_EMISSIVE)) {
            edit.ForEachTarget<StaticMeshPrimitiveComponent>([&registry](entt::entity e) { registry.emplace_or_replace<StaticMeshPrimitiveLoadingTag>(e); });
        }

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        edit.Set(&StaticMeshPrimitiveComponent::modelId, key);
                    }
                }
                ImGui::EndCombo();
            }
            return {.bRequestRemoval = remove};
        }

        const auto* modelMeta = ctx->assetManager->GetModelMetadata(component.modelId);
        ImGui::Text("Model: %s", edit.IsMixed(&StaticMeshPrimitiveComponent::modelId) ? "--" : modelMeta ? modelMeta->name.c_str() : "(invalid)");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            edit.Modify<StaticMeshPrimitiveComponent>([](StaticMeshPrimitiveComponent& c) {
                c.modelId = Engine::ModelID::INVALID;
                c.primitiveOrdinal = ~0u;
            });
            return {.bRequestRemoval = remove};
        }

        auto* meshRuntime = registry.try_get<MeshRuntime>(entity);
        Engine::StaticModel* model = meshRuntime ? ctx->assetManager->GetModel(meshRuntime->modelHandle) : nullptr;
        if (model && model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded) {
            const Core::HeapArray<Engine::Node>& nodes = model->modelData.nodes;
            const Core::HeapArray<Engine::MeshInformation>& meshes = model->modelData.meshes;

            uint32_t totalPrimitives = 0;
            for (uint32_t n = 0; n < nodes.Size(); ++n) {
                const uint32_t mi = nodes[n].meshIndex;
                if (mi == ~0u || mi >= meshes.Size()) { continue; }
                totalPrimitives += static_cast<uint32_t>(meshes[mi].primitiveProperties.Size());
            }

            if (totalPrimitives > 0) {
                const Core::InlineString<32> currentLabel = component.primitiveOrdinal < totalPrimitives
                    ? Core::InlineString<32>::Format("Primitive %u", component.primitiveOrdinal)
                    : Core::InlineString<32>("(none)");

                uint32_t pendingOrdinal = ~0u;
                if (ImGui::BeginCombo("Primitive", currentLabel.c_str())) {
                    for (uint32_t ord = 0; ord < totalPrimitives; ++ord) {
                        const Core::InlineString<32> label = Core::InlineString<32>::Format("Primitive %u", ord);
                        if (ImGui::Selectable(label.c_str(), ord == component.primitiveOrdinal)) {
                            if (ord != component.primitiveOrdinal) { pendingOrdinal = ord; }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (pendingOrdinal != ~0u) {
                    edit.Set(&StaticMeshPrimitiveComponent::primitiveOrdinal, pendingOrdinal);
                }
            }
        }
        else {
            ImGui::Text("Primitive Ordinal: %u", component.primitiveOrdinal);
        }

        const char* currentLabel = "__synthesized__";
        if (component.materialOverride.IsValid()) {
            if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.materialOverride)) { currentLabel = m->name.c_str(); }
        }
        Engine::MaterialID pendingMat{};
        bool changed = false;
        bool clear = false;
        if (ImGui::BeginCombo("Material", currentLabel, ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("__synthesized__", !component.materialOverride.IsValid())) {
                if (component.materialOverride.IsValid()) { clear = true; }
            }
            const Engine::MaterialID picked = Engine::DrawMaterialSelector(ctx, state, state->editor.materialSelector, component.materialOverride);
            if (picked.IsValid() && picked != component.materialOverride) {
                pendingMat = picked;
                changed = true;
            }
            ImGui::EndCombo();
        }
        if (changed || clear) {
            edit.Set(&StaticMeshPrimitiveComponent::materialOverride, clear ? Engine::MaterialID::INVALID : pendingMat);
        }

        ImGui::SeparatorText("Shader Overrides");
        {
            auto* pm = ctx->pipelineManager;
            Core::Span<const StringID> shadingPipelines = pm->GetShadingPipelines();
            Core::Arena& arena = ctx->editorArena.Get();
            Core::ArenaFixedVector<StringID> lightingPipelines = pm->GetLightingPipelinesForMode(viewFamily.lightingMode, arena);

            {
                const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
                int32_t current = -1;
                for (int32_t i = 0; i < pipelineCount; ++i) { if (component.shadingShaderOverride == shadingPipelines[i]) { current = i; break; } }
                Core::ArenaArray<Core::InlineString<64>> labels(&arena, pipelineCount + 1);
                labels[0] = Core::InlineString<64>("(none)");
                for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString<64>(shadingPipelines[i].ToString()); }
                int32_t comboIdx = current + 1;
                auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<64>>*>(data))[idx].c_str(); };
                if (ImGui::Combo("Shading", &comboIdx, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                    edit.Set(&StaticMeshPrimitiveComponent::shadingShaderOverride, comboIdx == 0 ? StringID{} : shadingPipelines[comboIdx - 1]);
                }
            }
            {
                const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
                int32_t current = -1;
                for (int32_t i = 0; i < pipelineCount; ++i) { if (component.lightingShaderOverride == lightingPipelines[i]) { current = i; break; } }
                Core::ArenaArray<Core::InlineString<64>> labels(&arena, pipelineCount + 1);
                labels[0] = Core::InlineString<64>("(none)");
                for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString<64>(lightingPipelines[i].ToString()); }
                int32_t comboIdx = current + 1;
                auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<64>>*>(data))[idx].c_str(); };
                if (ImGui::Combo("Lighting", &comboIdx, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                    edit.Set(&StaticMeshPrimitiveComponent::lightingShaderOverride, comboIdx == 0 ? StringID{} : lightingPipelines[comboIdx - 1]);
                }
            }
        }

        ImGui::SeparatorText("Render Transform");
        {
            EditWidgets::DragFloat3(edit, "Offset", &StaticMeshPrimitiveComponent::renderOffset, 0.1f);
            glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
            if (ImGui::DragFloat3("Rotation", &renderEuler.x, 0.5f)) {
                edit.PreviewSet(&StaticMeshPrimitiveComponent::renderRotation, glm::quat(glm::radians(renderEuler)));
            }
            EditWidgets::CommitOnRelease<StaticMeshPrimitiveComponent>(edit, false);
        }
    }

    return {.bRequestRemoval = remove};
}
}
