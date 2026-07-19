//
// Created by William on 2026-07-02.
//

#include "static_mesh_primitive_component.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include <glm/gtc/quaternion.hpp>

#include "core/containers/arena_array.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"
#include "game/components/render/static_mesh_component.h"

namespace Game::Component
{
void UnloadStaticMeshPrimitive(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<StaticMeshPrimitiveLoadPendingTag>(entity);
    registry.remove<StaticMeshPrimitiveLoadingTag>(entity);
}

void LoadStaticMeshPrimitive(StaticMeshPrimitiveComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.remove<StaticMeshPrimitiveLoadingTag>(entity);
    if (component.modelId.IsValid() && component.primitiveOrdinal != ~0u) {
        registry.emplace_or_replace<StaticMeshPrimitiveLoadPendingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
}

void StaticMeshPrimitiveComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
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
    return !registry.any_of<StaticMeshComponent>(entity);
}

void StaticMeshPrimitiveComponent::Serialize(const StaticMeshPrimitiveComponent& comp, nlohmann::json& json)
{
    json["modelId"] = comp.modelId.id;
    json["primitiveOrdinal"] = comp.primitiveOrdinal;
    json["modelFlags"] = {comp.modelFlags.x, comp.modelFlags.y, comp.modelFlags.z, comp.modelFlags.w};
    if (comp.materialOverride.IsValid()) { json["materialOverride"] = comp.materialOverride.id; }
    if (comp.shadingShaderOverride) { json["shadingShaderOverride"] = comp.shadingShaderOverride.id; }
    if (comp.lightingShaderOverride) { json["lightingShaderOverride"] = comp.lightingShaderOverride.id; }
    json["renderOffset"] = {comp.renderOffset.x, comp.renderOffset.y, comp.renderOffset.z};
    json["renderRotation"] = {comp.renderRotation.w, comp.renderRotation.x, comp.renderRotation.y, comp.renderRotation.z};
}

void StaticMeshPrimitiveComponent::Deserialize(StaticMeshPrimitiveComponent& comp, const nlohmann::json& json)
{
    comp.modelId = Engine::ModelID(json["modelId"].get<uint64_t>());
    comp.primitiveOrdinal = json["primitiveOrdinal"].get<uint32_t>();
    if (json.contains("modelFlags")) {
        const auto& f = json["modelFlags"];
        comp.modelFlags = glm::vec4(f[0].get<float>(), f[1].get<float>(), f[2].get<float>(), f[3].get<float>());
    }
    if (json.contains("materialOverride")) { comp.materialOverride = Engine::MaterialID(json["materialOverride"].get<uint64_t>()); }
    if (json.contains("shadingShaderOverride")) { comp.shadingShaderOverride = StringID(json["shadingShaderOverride"].get<uint64_t>()); }
    if (json.contains("lightingShaderOverride")) { comp.lightingShaderOverride = StringID(json["lightingShaderOverride"].get<uint64_t>()); }
    if (json.contains("renderOffset")) {
        const auto& o = json["renderOffset"];
        comp.renderOffset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }
    if (json.contains("renderRotation")) {
        const auto& r = json["renderRotation"];
        comp.renderRotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
    }
}

Engine::ComponentEditorResult StaticMeshPrimitiveComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<StaticMeshPrimitiveComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    bool open = ImGui::CollapsingHeader("Static Mesh Primitive", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestaticmeshprimitive");
    ImGui::PopStyleColor();

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        bool hero = component.modelFlags.w != 0.0f;
        if (ImGui::Checkbox("Visible", &visible)) { component.modelFlags.x = visible ? 1.0f : 0.0f; }
        ImGui::SameLine();
        if (ImGui::Checkbox("Hero", &hero)) { component.modelFlags.w = hero ? 1.0f : 0.0f; }

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        LoadStaticMeshPrimitive(component, registry, entity);
                    }
                }
                ImGui::EndCombo();
            }
            return {.requestRemoval = remove};
        }

        const auto* modelMeta = ctx->assetManager->GetModelMetadata(component.modelId);
        ImGui::Text("Model: %s", modelMeta ? modelMeta->name.c_str() : "(invalid)");
        ImGui::SameLine();
        if (ImGui::SmallButton("X##deselect_model")) {
            UnloadStaticMeshPrimitive(registry, entity);
            component.modelId = Engine::ModelID::INVALID;
            component.primitiveOrdinal = ~0u;
            registry.remove<RenderTransformComponent>(entity);
            registry.remove<MultiframeDirtyTransformComponent>(entity);
            return {.requestRemoval = remove};
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
                    component.primitiveOrdinal = pendingOrdinal;
                    registry.emplace_or_replace<StaticMeshPrimitiveLoadingTag>(entity);
                    state->bPendingModelResolve |= true;
                }
            }
        }
        else {
            ImGui::Text("Primitive Ordinal: %u", component.primitiveOrdinal);
        }

        const char* currentLabel = "(original)";
        if (component.materialOverride.IsValid()) {
            if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.materialOverride)) { currentLabel = m->name.c_str(); }
        }
        Engine::MaterialID pendingMat{};
        bool changed = false;
        bool clear = false;
        if (ImGui::BeginCombo("Material", currentLabel)) {
            if (ImGui::Selectable("(original)", !component.materialOverride.IsValid())) {
                if (component.materialOverride.IsValid()) { clear = true; }
            }
            for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                if (mat.immutable) { continue; }
                if (ImGui::Selectable(mat.name.c_str(), matId == component.materialOverride)) {
                    if (matId != component.materialOverride) { pendingMat = matId; changed = true; }
                }
            }
            ImGui::EndCombo();
        }
        if (changed || clear) {
            component.materialOverride = clear ? Engine::MaterialID::INVALID : pendingMat;
            registry.emplace_or_replace<StaticMeshPrimitiveLoadingTag>(entity);
            state->bPendingModelResolve |= true;
        }

        ImGui::SeparatorText("Shader Overrides");
        {
            auto* pm = ctx->pipelineManager;
            Core::Span<const StringID> shadingPipelines = pm->GetShadingPipelines();
            Core::Span<const StringID> lightingPipelines = pm->GetLightingPipelines();
            Core::Arena& arena = ctx->editorArena.Get();
            bool shaderChanged = false;

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
                    component.shadingShaderOverride = comboIdx == 0 ? StringID{} : shadingPipelines[comboIdx - 1];
                    shaderChanged = true;
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
                    component.lightingShaderOverride = comboIdx == 0 ? StringID{} : lightingPipelines[comboIdx - 1];
                    shaderChanged = true;
                }
            }
            if (shaderChanged) {
                registry.emplace_or_replace<StaticMeshPrimitiveLoadingTag>(entity);
                state->bPendingModelResolve |= true;
            }
        }

        ImGui::SeparatorText("Render Transform");
        {
            bool changedTransform = ImGui::DragFloat3("Offset", &component.renderOffset.x, 0.1f);
            glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
            if (ImGui::DragFloat3("Rotation", &renderEuler.x, 0.5f)) {
                component.renderRotation = glm::quat(glm::radians(renderEuler));
                changedTransform = true;
            }
            if (changedTransform) {
                if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
                    rt->renderOffset = component.renderOffset;
                    rt->renderRotation = component.renderRotation;
                    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
                }
            }
        }
    }

    return {.requestRemoval = remove};
}
}
