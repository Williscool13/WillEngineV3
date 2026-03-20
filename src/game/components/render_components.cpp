//
// Created by William on 2026-01-30.
//

#include "render_components.h"

#include <entt/entt.hpp>
#include <json/nlohmann/json.hpp>

#include "imgui.h"
#include <glm/gtc/type_ptr.hpp>
#include <ImGuizmo.h>

#include "scene_components.h"
#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/systems/editor_systems.h"
#include "game/component-registry/component_copy.h"
#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_initialization.h"

namespace Game
{
template<>
bool CanAddComponent<Component::StaticMeshComponent>(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::ProceduralMeshComponent, Component::SplineMeshComponent>(entity);
}

template<>
bool CanAddComponent<Component::ProceduralMeshComponent>(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::SplineMeshComponent>(entity);
}

template<>
bool CanAddComponent<Component::SplineMeshComponent>(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::ProceduralMeshComponent>(entity);
}

template<>
Component::StaticMeshComponent CopyComponent(const Component::StaticMeshComponent& src, entt::registry& dstReg)
{
    Component::StaticMeshComponent copy{};
    copy.modelFlags = src.modelFlags;
    copy.modelId = src.modelId;
    copy.meshIndex = src.meshIndex;
    return copy;
}

template<>
void SerializeComponent<Component::StaticMeshComponent>(const Component::StaticMeshComponent& comp, nlohmann::json& json)
{
    json["meshIndex"] = comp.meshIndex;
    json["modelId"] = comp.modelId.id;

    nlohmann::json overrides = nlohmann::json::object();
    for (int32_t i = 0; i < 128; ++i) {
        if (comp.materialOverrides[i].IsValid()) {
            overrides[std::to_string(i)] = comp.materialOverrides[i].id;
        }
    }
    if (!overrides.empty()) {
        json["materialOverrides"] = std::move(overrides);
    }
}

template<>
void DeserializeComponent<Component::StaticMeshComponent>(Component::StaticMeshComponent& comp, const nlohmann::json& json)
{
    comp.meshIndex = json["meshIndex"].get<int32_t>();
    comp.modelId = Engine::ModelID(json["modelId"].get<uint64_t>());

    if (json.contains("materialOverrides")) {
        for (const auto& [key, val] : json["materialOverrides"].items()) {
            int32_t idx = std::stoi(key);
            if (idx >= 0 && idx < 128) {
                comp.materialOverrides[idx] = Engine::MaterialID(val.get<uint64_t>());
            }
        }
    }
}
}

namespace Game
{
template<>
ComponentEditorResult DrawComponentEditor<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                          entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletestaticmesh");
    ImGui::PopStyleColor();

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        bool shadowCaster = component.modelFlags.y != 0.0f;
        if (ImGui::Checkbox("Visible", &visible)) {
            component.modelFlags.x = visible ? 1.0f : 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow Caster", &shadowCaster)) {
            component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;
        }

        auto* ctx = registry.ctx().get<Core::EngineContext*>();
        auto* state = registry.ctx().get<Engine::GameState*>();

        if (!component.modelId.IsValid()) {
            if (ImGui::BeginCombo("Select Model", "")) {
                const auto& modelCache = ctx->assetManager->GetModelCache();
                for (const auto& [key, meta] : modelCache) {
                    if (ImGui::Selectable(meta.name.c_str(), false)) {
                        component.modelId = key;
                        component.modelHandle = ctx->assetManager->LoadModel(component.modelId);
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
            ctx->assetManager->UnloadModel(component.modelHandle);
            component.modelId = Engine::ModelID::INVALID;
            component.modelHandle = {};
            component.meshIndex = -1;
            component.primitiveCount = 0;
            registry.remove<Component::StaticMeshLoadingTag>(entity);
            registry.remove<Component::RenderTransformComponent>(entity);
            registry.remove<Component::DirtyRenderTransformComponent>(entity);
            return {.requestRemoval = remove};
        }

        if (!component.modelHandle.IsValid()) {
            LOG_WARN(Game, "modelId specified but model handle is invalid, resetting to unset");
            component.modelId = Engine::ModelID::INVALID;
            return {.requestRemoval = remove};
        }
        Engine::StaticModel* model = ctx->assetManager->GetModel(component.modelHandle);
        if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
            ImGui::Text("Loading Model...");
            return {.requestRemoval = remove};
        }

        if (component.meshIndex == -1) {
            if (model->modelData.meshes.size() == 1) {
                component.meshIndex = 0;
                Component::RecreateStaticMesh(component, registry, entity);
            }
            else {
                if (ImGui::BeginCombo("Select Mesh", "")) {
                    for (int32_t i = 0; i < model->modelData.meshes.size(); i++) {
                        auto _name = model->modelData.meshes[i].name;
                        if (_name.empty()) {
                            _name = fmt::format("Mesh {}", i);
                        }
                        if (ImGui::Selectable(_name.c_str(), false)) {
                            component.meshIndex = i;
                            Component::RecreateStaticMesh(component, registry, entity);
                        }
                    }
                    ImGui::EndCombo();
                }
                return {.requestRemoval = remove};
            }
        }

        ImGui::Text("Mesh Index: %d", component.meshIndex);
        if (model->modelData.meshes.size() > 1) {
            if (ImGui::SmallButton("X##deselect_mesh")) {
                component.meshIndex = -1;
                component.primitiveCount = 0;
                registry.remove<Component::StaticMeshLoadingTag>(entity);
                registry.remove<Component::RenderTransformComponent>(entity);
                registry.remove<Component::DirtyRenderTransformComponent>(entity);
                return {.requestRemoval = remove};
            }
        }

        ImGui::Text("Primitive Count: %u", component.primitiveCount);

        if (component.primitiveCount > 0 && ImGui::TreeNode("Primitives")) {
            for (uint8_t i = 0; i < component.primitiveCount; ++i) {
                ImGui::PushID(i);
                if (ImGui::TreeNode("", "Primitive %u", i)) {
                    const auto& prim = component.primitives[i];
                    ImGui::Text("Primitive Index: %u", prim.primitiveIndex);
                    ImGui::Text("Material ID: %llu", prim.materialID.id);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (component.primitiveCount > 0) {
            struct SlotInfo
            {
                int32_t origIdx;
                std::string name;
            };
            std::vector<SlotInfo> slots;
            bool seen[128] = {};
            for (uint8_t i = 0; i < component.primitiveCount; ++i) {
                int32_t idx = component.primitives[i].originalMaterialIndex;
                if (idx < 0 || idx >= 128 || seen[idx]) continue;
                seen[idx] = true;
                std::string slotName;
                if (idx < static_cast<int32_t>(model->modelData.materials.size()) &&
                    !model->modelData.materials[idx].name.empty()) {
                    slotName = model->modelData.materials[idx].name;
                }
                else {
                    slotName = fmt::format("Material {}", idx);
                }
                slots.push_back({idx, std::move(slotName)});
            }

            if (!slots.empty() && ImGui::TreeNode("Material Overrides")) {
                const auto& allMaterials = ctx->materialManager->GetMaterials();
                int32_t pendingChangeIdx = -1;
                Engine::MaterialID pendingChangeMat{};

                for (const auto& slot : slots) {
                    ImGui::PushID(slot.origIdx);

                    Engine::MaterialID current = component.materialOverrides[slot.origIdx];
                    const char* currentLabel = "(original)";
                    if (current.IsValid()) {
                        if (const Engine::Material* m = ctx->materialManager->GetMaterial(current)) {
                            currentLabel = m->name.c_str();
                        }
                    }

                    ImGui::Text("%s", slot.name.c_str());
                    ImGui::SameLine();

                    if (ImGui::BeginCombo("##override", currentLabel)) {
                        if (ImGui::Selectable("(original)", !current.IsValid())) {
                            if (current.IsValid()) {
                                pendingChangeIdx = slot.origIdx;
                                pendingChangeMat = Engine::MaterialID::INVALID;
                            }
                        }
                        for (const auto& [matId, mat] : allMaterials) {
                            if (mat.immutable) continue;
                            if (ImGui::Selectable(mat.name.c_str(), matId == current)) {
                                if (matId != current) {
                                    pendingChangeIdx = slot.origIdx;
                                    pendingChangeMat = matId;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::PopID();
                }

                if (pendingChangeIdx >= 0) {
                    component.materialOverrides[pendingChangeIdx] = pendingChangeMat;
                    registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
                    state->bPendingModelResolve |= true;
                }

                ImGui::TreePop();
            }
        }
    }

    return {.requestRemoval = remove};
}

void Component::RecreateStaticMesh(StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    // Teardown
    for (size_t i = 0; i < component.primitiveCount; ++i) {
        ctx->materialManager->ReleaseMaterial(component.primitives[i].materialID);
    }
    component.primitiveCount = 0;
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
        component.modelHandle = {};
    }

    // Rebuild
    if (component.modelId.IsValid() && component.meshIndex != -1) {
        component.modelHandle = ctx->assetManager->LoadModel(component.modelId);
        registry.emplace_or_replace<StaticMeshLoadingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

void Component::RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    // Teardown
    if (component.bPrimitiveReady) {
        ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
        component.bPrimitiveReady = false;
    }
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
        component.modelHandle = {};
    }

    // Rebuild
    if (!std::holds_alternative<std::monostate>(component.params)) {
        component.modelHandle = ctx->assetManager->LoadProceduralModel(component.params);
        registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}

template<>
void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{

    if (component.meshIndex == -1) {
        return;
    }

    Component::RecreateStaticMesh(component, registry, entity);
}

template<>
void OnComponentRemoved<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    for (size_t i = 0; i < component.primitiveCount; ++i) {
        ctx->materialManager->ReleaseMaterial(component.primitives[i].materialID);
    }
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
    }

    registry.remove<Component::StaticMeshLoadingTag>(entity);
    registry.remove<Component::RenderTransformComponent>(entity);
    registry.remove<Component::DirtyRenderTransformComponent>(entity);
    registry.remove<Component::StaticMeshComponent>(entity);
}

template<>
Component::ProceduralMeshComponent CopyComponent(const Component::ProceduralMeshComponent& src, entt::registry& dstReg)
{
    Component::ProceduralMeshComponent copy{};
    copy.params = src.params;
    copy.material = src.material;
    copy.modelFlags = src.modelFlags;
    return copy;
}

template<>
void SerializeComponent<Component::ProceduralMeshComponent>(const Component::ProceduralMeshComponent& comp, nlohmann::json& json)
{
    json["type"] = comp.params.index();
    json["material"] = comp.material.id;

    std::visit([&json](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
            json["stepCount"] = p.stepCount;
            json["width"] = p.width;
            json["totalDepth"] = p.totalDepth;
            json["totalHeight"] = p.totalHeight;
            json["bSpecifyStepHeight"] = p.bSpecifyStepHeight;
            json["stepHeight"] = p.stepHeight;
            json["bIsClosed"] = p.bIsClosed;
        }
        else if constexpr (std::is_same_v<T, Engine::BoxParams>) {
            json["sizeX"] = p.sizeX;
            json["sizeY"] = p.sizeY;
            json["sizeZ"] = p.sizeZ;
        }
        else if constexpr (std::is_same_v<T, Engine::CylinderParams>) {
            json["radius"] = p.radius;
            json["height"] = p.height;
            json["slices"] = p.slices;
            json["bCapped"] = p.bCapped;
        }
        else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
            json["radius"] = p.radius;
            json["height"] = p.height;
            json["slices"] = p.slices;
            json["rings"] = p.rings;
        }
        else if constexpr (std::is_same_v<T, Engine::TorusParams>) {
            json["ringRadius"] = p.ringRadius;
            json["tubeRadius"] = p.tubeRadius;
            json["slices"] = p.slices;
            json["stacks"] = p.stacks;
        }
        else if constexpr (std::is_same_v<T, Engine::ArchParams>) {
            json["width"] = p.width;
            json["height"] = p.height;
            json["depth"] = p.depth;
            json["thickness"] = p.thickness;
            json["sides"] = p.sides;
        }
        else if constexpr (std::is_same_v<T, Engine::WedgeParams>) {
            json["sizeX"] = p.sizeX;
            json["sizeY"] = p.sizeY;
            json["sizeZ"] = p.sizeZ;
        }
        else if constexpr (std::is_same_v<T, Engine::ConeParams>) {
            json["radius"] = p.radius;
            json["height"] = p.height;
            json["slices"] = p.slices;
            json["bCapped"] = p.bCapped;
        }
        else if constexpr (std::is_same_v<T, Engine::DoorParams>) {
            json["width"] = p.width;
            json["height"] = p.height;
            json["depth"] = p.depth;
            json["archHeight"] = p.archHeight;
            json["gap"] = p.gap;
            json["sides"] = p.sides;
            json["bHalf"] = p.bHalf;
            json["bFlip"] = p.bFlip;
        }
        else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
            json["sizeX"] = p.sizeX;
            json["sizeZ"] = p.sizeZ;
            json["tilesX"] = p.tilesX;
            json["tilesZ"] = p.tilesZ;
        }
        else if constexpr (std::is_same_v<T, Engine::SphereParams>) {
            json["radius"] = p.radius;
            json["slices"] = p.slices;
            json["stacks"] = p.stacks;
        }
        else if constexpr (std::is_same_v<T, Engine::SubdividedSphereParams>) {
            json["radius"] = p.radius;
            json["subdivisions"] = p.subdivisions;
        }
        else if constexpr (std::is_same_v<T, Engine::HemisphereParams>) {
            json["radius"] = p.radius;
            json["slices"] = p.slices;
            json["stacks"] = p.stacks;
        }
        else if constexpr (std::is_same_v<T, Engine::PipeParams>) {
            json["outerRadius"] = p.outerRadius;
            json["innerRadius"] = p.innerRadius;
            json["height"] = p.height;
            json["slices"] = p.slices;
        }
        else if constexpr (std::is_same_v<T, Engine::TetrahedronParams> ||
                           std::is_same_v<T, Engine::OctahedronParams> ||
                           std::is_same_v<T, Engine::IcosahedronParams> ||
                           std::is_same_v<T, Engine::DodecahedronParams>) {
            json["radius"] = p.radius;
        }
        else if constexpr (std::is_same_v<T, Engine::KleinBottleParams>) {
            json["scale"] = p.scale;
            json["slices"] = p.slices;
            json["stacks"] = p.stacks;
        }
        else if constexpr (std::is_same_v<T, Engine::TrefoilKnotParams>) {
            json["scale"] = p.scale;
            json["tubeRadius"] = p.tubeRadius;
            json["slices"] = p.slices;
            json["stacks"] = p.stacks;
        }
    }, comp.params);
}

template<>
void DeserializeComponent<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& comp, const nlohmann::json& json)
{
    comp.material = Engine::MaterialID(json["material"].get<uint64_t>());

    int32_t type = json["type"].get<int32_t>();
    if (type == 1) {
        Engine::StaircaseParams p{};
        p.stepCount = json["stepCount"].get<int32_t>();
        p.width = json["width"].get<float>();
        p.totalDepth = json["totalDepth"].get<float>();
        p.totalHeight = json["totalHeight"].get<float>();
        p.bSpecifyStepHeight = json.value("bSpecifyStepHeight", false);
        p.stepHeight = json.value("stepHeight", p.totalHeight / static_cast<float>(std::max(p.stepCount, 1)));
        p.bIsClosed = json.value("bIsClosed", true);
        comp.params = p;
    }
    else if (type == 2) {
        Engine::BoxParams p{};
        p.sizeX = json["sizeX"].get<float>();
        p.sizeY = json["sizeY"].get<float>();
        p.sizeZ = json["sizeZ"].get<float>();
        comp.params = p;
    }
    else if (type == 3) {
        Engine::CylinderParams p{};
        p.radius = json["radius"].get<float>();
        p.height = json["height"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.bCapped = json["bCapped"].get<bool>();
        comp.params = p;
    }
    else if (type == 4) {
        Engine::CapsuleParams p{};
        p.radius = json["radius"].get<float>();
        p.height = json["height"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.rings = json["rings"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 5) {
        Engine::TorusParams p{};
        p.ringRadius = json["ringRadius"].get<float>();
        p.tubeRadius = json["tubeRadius"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.stacks = json["stacks"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 6) {
        Engine::ArchParams p{};
        p.width = json["width"].get<float>();
        p.height = json["height"].get<float>();
        p.depth = json["depth"].get<float>();
        p.thickness = json["thickness"].get<float>();
        p.sides = json["sides"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 7) {
        Engine::WedgeParams p{};
        p.sizeX = json["sizeX"].get<float>();
        p.sizeY = json["sizeY"].get<float>();
        p.sizeZ = json["sizeZ"].get<float>();
        comp.params = p;
    }
    else if (type == 8) {
        Engine::ConeParams p{};
        p.radius = json["radius"].get<float>();
        p.height = json["height"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.bCapped = json["bCapped"].get<bool>();
        comp.params = p;
    }
    else if (type == 9) {
        Engine::DoorParams p{};
        p.width = json["width"].get<float>();
        p.height = json["height"].get<float>();
        p.depth = json["depth"].get<float>();
        p.archHeight = json.value("archHeight", 0.5f);
        p.gap = json.value("gap", 0.0f);
        p.sides = json["sides"].get<int32_t>();
        p.bHalf = json["bHalf"].get<bool>();
        p.bFlip = json.value("bFlip", false);
        comp.params = p;
    }
    else if (type == 10) {
        Engine::PlaneParams p{};
        p.sizeX = json["sizeX"].get<float>();
        p.sizeZ = json["sizeZ"].get<float>();
        p.tilesX = json["tilesX"].get<int32_t>();
        p.tilesZ = json["tilesZ"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 11) {
        Engine::SphereParams p{};
        p.radius = json["radius"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.stacks = json["stacks"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 12) {
        Engine::SubdividedSphereParams p{};
        p.radius = json["radius"].get<float>();
        p.subdivisions = glm::clamp(json["subdivisions"].get<int32_t>(), 0, 4);
        comp.params = p;
    }
    else if (type == 13) {
        Engine::HemisphereParams p{};
        p.radius = json["radius"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.stacks = json["stacks"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 14) {
        Engine::PipeParams p{};
        p.outerRadius = json["outerRadius"].get<float>();
        p.innerRadius = json["innerRadius"].get<float>();
        p.height = json["height"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 15) {
        Engine::TetrahedronParams p{};
        p.radius = json["radius"].get<float>();
        comp.params = p;
    }
    else if (type == 16) {
        Engine::OctahedronParams p{};
        p.radius = json["radius"].get<float>();
        comp.params = p;
    }
    else if (type == 17) {
        Engine::IcosahedronParams p{};
        p.radius = json["radius"].get<float>();
        comp.params = p;
    }
    else if (type == 18) {
        Engine::DodecahedronParams p{};
        p.radius = json["radius"].get<float>();
        comp.params = p;
    }
    else if (type == 19) {
        Engine::KleinBottleParams p{};
        p.scale = json["scale"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.stacks = json["stacks"].get<int32_t>();
        comp.params = p;
    }
    else if (type == 20) {
        Engine::TrefoilKnotParams p{};
        p.scale = json["scale"].get<float>();
        p.tubeRadius = json["tubeRadius"].get<float>();
        p.slices = json["slices"].get<int32_t>();
        p.stacks = json["stacks"].get<int32_t>();
        comp.params = p;
    }
}

template<>
ComponentEditorResult DrawComponentEditor<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                              entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Procedural Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteproceduralmesh");
    ImGui::PopStyleColor();

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        bool shadowCaster = component.modelFlags.y != 0.0f;
        if (ImGui::Checkbox("Visible##proceduralmesh", &visible)) {
            component.modelFlags.x = visible ? 1.0f : 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow Caster##proceduralmesh", &shadowCaster)) {
            component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;
        }

        auto* ctx = registry.ctx().get<Core::EngineContext*>();
        auto* state = registry.ctx().get<Engine::GameState*>();

        if (std::holds_alternative<std::monostate>(component.params)) {
            if (ImGui::BeginCombo("Shape", "")) {
                auto selectShape = [&](auto&& params) {
                    component.params = std::move(params);
                    Component::RecreateProceduralMesh(component, registry, entity);
                };
                if (ImGui::Selectable("Staircase")) selectShape(Engine::StaircaseParams{});
                if (ImGui::Selectable("Box")) selectShape(Engine::BoxParams{});
                if (ImGui::Selectable("Cylinder")) selectShape(Engine::CylinderParams{});
                if (ImGui::Selectable("Capsule")) selectShape(Engine::CapsuleParams{});
                if (ImGui::Selectable("Torus")) selectShape(Engine::TorusParams{});
                if (ImGui::Selectable("Arch")) selectShape(Engine::ArchParams{});
                if (ImGui::Selectable("Wedge")) selectShape(Engine::WedgeParams{});
                if (ImGui::Selectable("Cone")) selectShape(Engine::ConeParams{});
                if (ImGui::Selectable("Door")) selectShape(Engine::DoorParams{});
                if (ImGui::Selectable("Plane")) selectShape(Engine::PlaneParams{});
                if (ImGui::Selectable("Sphere")) selectShape(Engine::SphereParams{});
                if (ImGui::Selectable("Subdivided Sphere")) selectShape(Engine::SubdividedSphereParams{});
                if (ImGui::Selectable("Hemisphere")) selectShape(Engine::HemisphereParams{});
                if (ImGui::Selectable("Pipe")) selectShape(Engine::PipeParams{});
                if (ImGui::Selectable("Tetrahedron")) selectShape(Engine::TetrahedronParams{});
                if (ImGui::Selectable("Octahedron")) selectShape(Engine::OctahedronParams{});
                if (ImGui::Selectable("Icosahedron")) selectShape(Engine::IcosahedronParams{});
                if (ImGui::Selectable("Dodecahedron")) selectShape(Engine::DodecahedronParams{});
                if (ImGui::Selectable("Klein Bottle")) selectShape(Engine::KleinBottleParams{});
                if (ImGui::Selectable("Trefoil Knot")) selectShape(Engine::TrefoilKnotParams{});
                ImGui::EndCombo();
            }
        }
        else {
            static constexpr const char* shapeNames[] = {
                "", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
                "Icosahedron", "Dodecahedron", "Klein Bottle", "Trefoil Knot"
            };
            ImGui::Text("Shape: %s", shapeNames[component.params.index()]);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##deselect_shape")) {
                component.params = std::monostate{};
                Component::RecreateProceduralMesh(component, registry, entity);
            }
            ImGui::PopStyleColor();

            bool dirty = false;
            std::visit([&dirty](auto& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Total Depth", &p.totalDepth, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();

                    // Total Height — always editable; drives stepCount when bSpecifyStepHeight
                    ImGui::DragFloat("Total Height", &p.totalHeight, 0.01f, 0.01f, 100.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        if (p.bSpecifyStepHeight) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                        }
                        dirty = true;
                    }

                    if (ImGui::Checkbox("Specify Step Height", &p.bSpecifyStepHeight)) {
                        if (p.bSpecifyStepHeight) {
                            p.stepHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        }
                    }

                    // Step Count — editable when !bSpecifyStepHeight, greyed float when derived
                    if (p.bSpecifyStepHeight) {
                        float derivedCount = p.totalHeight / std::max(p.stepHeight, 0.001f);
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Count", &derivedCount, 1.0f, 1.0f, 256.0f, "%.2f");
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::DragInt("Step Count", &p.stepCount, 1, 1, 256);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }

                    // Step Height — editable when bSpecifyStepHeight, greyed derived otherwise
                    if (p.bSpecifyStepHeight) {
                        ImGui::DragFloat("Step Height", &p.stepHeight, 0.001f, 0.001f, 10.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                            dirty = true;
                        }
                    }
                    else {
                        float derivedHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Height", &derivedHeight, 0.001f, 0.001f, 10.0f);
                        ImGui::EndDisabled();
                    }

                    if (ImGui::Checkbox("Closed", &p.bIsClosed)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::BoxParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::CylinderParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Capped", &p.bCapped)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Rings", &p.rings, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TorusParams>) {
                    ImGui::DragFloat("Ring Radius", &p.ringRadius, 0.01f, 0.01f, 50.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.tubeRadius = glm::clamp(p.tubeRadius, p.ringRadius * 0.1f, p.ringRadius * 0.99f);
                        dirty = true;
                    }
                    ImGui::DragFloat("Tube Radius", &p.tubeRadius, 0.01f, 0.001f, p.ringRadius * 0.99f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.tubeRadius = glm::clamp(p.tubeRadius, p.ringRadius * 0.1f, p.ringRadius * 0.99f);
                        dirty = true;
                    }
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::ArchParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Depth", &p.depth, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Thickness", &p.thickness, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Sides", &p.sides, 1, 1, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::WedgeParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::ConeParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Capped", &p.bCapped)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::DoorParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Depth", &p.depth, 0.001f, 0.001f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Arch Height", &p.archHeight, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Gap", &p.gap, 0.001f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Sides", &p.sides, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Half", &p.bHalf)) { dirty = true; }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Flip (right-side hinge)", &p.bFlip)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 1000.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 1000.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Tiles X", &p.tilesX, 1, 1, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Tiles Z", &p.tilesZ, 1, 1, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::SphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::SubdividedSphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Subdivisions", &p.subdivisions, 1, 0, 4);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::HemisphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::PipeParams>) {
                    ImGui::DragFloat("Outer Radius", &p.outerRadius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Inner Radius", &p.innerRadius, 0.01f, 0.001f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TetrahedronParams> ||
                                   std::is_same_v<T, Engine::OctahedronParams> ||
                                   std::is_same_v<T, Engine::IcosahedronParams> ||
                                   std::is_same_v<T, Engine::DodecahedronParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::KleinBottleParams>) {
                    ImGui::TextDisabled("Requires double-sided material");
                    ImGui::DragFloat("Scale", &p.scale, 0.01f, 0.001f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TrefoilKnotParams>) {
                    ImGui::DragFloat("Scale", &p.scale, 0.01f, 0.001f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Tube Radius", &p.tubeRadius, 0.1f, 0.5f, 3.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 512);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }, component.params);

            if (dirty) {
                Component::RecreateProceduralMesh(component, registry, entity);
            }
        }

        // Material selector
        {
            const char* currentLabel = "(none)";
            if (component.material.IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.material)) {
                    currentLabel = m->name.c_str();
                }
            }
            if (ImGui::BeginCombo("Material", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        if (component.bPrimitiveReady) {
                            ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
                            component.bPrimitiveReady = false;
                        }
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<Component::ProceduralMeshLoadingTag>(entity);
                        state->bPendingModelResolve |= true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) continue;
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            if (component.bPrimitiveReady) {
                                ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
                                component.bPrimitiveReady = false;
                            }
                            component.material = matId;
                            registry.emplace_or_replace<Component::ProceduralMeshLoadingTag>(entity);
                            state->bPendingModelResolve |= true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }
    }

    return {.requestRemoval = remove};
}

template<>
void OnComponentAdded<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    Component::RecreateProceduralMesh(component, registry, entity);
}

template<>
void OnComponentRemoved<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    if (component.bPrimitiveReady) {
        ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
    }
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
    }

    registry.remove<Component::ProceduralMeshLoadingTag>(entity);
    registry.remove<Component::RenderTransformComponent>(entity);
    registry.remove<Component::DirtyRenderTransformComponent>(entity);
    registry.remove<Component::ProceduralMeshComponent>(entity);
}

template<>
Component::SplineMeshComponent CopyComponent(const Component::SplineMeshComponent& src, entt::registry& dstReg)
{
    Component::SplineMeshComponent copy{};
    copy.controlPoints = src.controlPoints;
    copy.radius = src.radius;
    copy.rollAngle = src.rollAngle;
    copy.sides = src.sides;
    copy.segmentsPerSpan = src.segmentsPerSpan;
    copy.bClosed = src.bClosed;
    copy.bCaps = src.bCaps;
    copy.material = src.material;
    copy.modelFlags = src.modelFlags;
    return copy;
}

template<>
void SerializeComponent<Component::SplineMeshComponent>(const Component::SplineMeshComponent& comp, nlohmann::json& json)
{
    json["radius"] = comp.radius;
    json["rollAngle"] = comp.rollAngle;
    json["sides"] = comp.sides;
    json["segmentsPerSpan"] = comp.segmentsPerSpan;
    json["bClosed"] = comp.bClosed;
    json["bCaps"] = comp.bCaps;
    json["material"] = comp.material.id;

    json["controlPoints"] = nlohmann::json::array();
    for (const auto& cp : comp.controlPoints) {
        json["controlPoints"].push_back({cp.x, cp.y, cp.z});
    }
}

template<>
void DeserializeComponent<Component::SplineMeshComponent>(Component::SplineMeshComponent& comp, const nlohmann::json& json)
{
    comp.radius = json["radius"].get<float>();
    comp.rollAngle = json.value("rollAngle", 0.0f);
    comp.sides = json["sides"].get<int32_t>();
    comp.segmentsPerSpan = json["segmentsPerSpan"].get<int32_t>();
    comp.bClosed = json.value("bClosed", false);
    comp.bCaps = json.value("bCaps", true);
    comp.material = Engine::MaterialID(json["material"].get<uint64_t>());

    comp.controlPoints.clear();
    for (const auto& cpJson : json["controlPoints"]) {
        comp.controlPoints.push_back({cpJson[0].get<float>(), cpJson[1].get<float>(), cpJson[2].get<float>()});
    }
    if (comp.controlPoints.size() < 2) {
        comp.controlPoints = {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}};
    }
}

template<>
ComponentEditorResult DrawComponentEditor<Component::SplineMeshComponent>(Component::SplineMeshComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                          const char* name)
{
    static int editPointIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wasUsingGizmo = false;

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    if (editEntity != entity) {
        editPointIdx = -1;
        editEntity = entity;
        wasUsingGizmo = false;
    }

    bool hasGizmoClaim = editPointIdx != -1 && !state->bCustomGizmoActive;


    bool open = ImGui::CollapsingHeader("Spline Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletesplinemesh");
    ImGui::PopStyleColor();

    if (open) {
        bool visible = component.modelFlags.x != 0.0f;
        bool shadowCaster = component.modelFlags.y != 0.0f;
        if (ImGui::Checkbox("Visible##splinemesh", &visible)) component.modelFlags.x = visible ? 1.0f : 0.0f;
        ImGui::SameLine();
        if (ImGui::Checkbox("Shadow Caster##splinemesh", &shadowCaster)) component.modelFlags.y = shadowCaster ? 1.0f : 0.0f;

        bool dirty = false;

        ImGui::DragFloat("Radius", &component.radius, 0.01f, 0.001f, 50.0f);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Roll Angle", &component.rollAngle, 1.0f, -180.0f, 180.0f, "%.1f deg");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Sides", &component.sides, 1, 3, 64);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragInt("Segments/Span", &component.segmentsPerSpan, 1, 1, 32);
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Caps", &component.bCaps)) { dirty = true; }
        ImGui::SameLine();
        if (ImGui::Checkbox("Closed", &component.bClosed)) { dirty = true; }

        ImGui::SeparatorText("Control Points");

        int pointToRemove = -1;
        for (int i = 0; i < static_cast<int>(component.controlPoints.size()); i++) {
            ImGui::PushID(i);
            const bool isEditing = (editPointIdx == i);

            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? ImVec4(0.15f, 0.65f, 0.15f, 1.0f) : ImVec4(0.15f, 0.35f, 0.65f, 1.0f));
            ImGui::BeginDisabled((state->bCustomGizmoActive || state->bCustomGizmoActivePrev) && !isEditing);
            if (ImGui::SmallButton(isEditing ? "D##edit" : "E##edit")) {
                editPointIdx = isEditing ? -1 : i;
                if (editPointIdx == -1) { hasGizmoClaim = false; }
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char label[16];
            snprintf(label, sizeof(label), "##cp%d", i);
            if (ImGui::DragFloat3(label, &component.controlPoints[i].x, 0.01f)) {}
            dirty |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();

            ImGui::BeginDisabled(static_cast<int>(component.controlPoints.size()) <= 2);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##rmpt")) {
                pointToRemove = i;
                if (editPointIdx == i) { editPointIdx = -1; }
                else if (editPointIdx > i) { editPointIdx--; }
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();

            ImGui::PopID();
        }

        if (pointToRemove >= 0) {
            component.controlPoints.erase(component.controlPoints.begin() + pointToRemove);
            dirty = true;
        }

        if (ImGui::Button("Add Point")) {
            const glm::vec3 last = component.controlPoints.back();
            const glm::vec3 prev = component.controlPoints.size() >= 2
                                       ? component.controlPoints[component.controlPoints.size() - 2]
                                       : last - glm::vec3(0, 0, 1);
            component.controlPoints.push_back(last + glm::normalize(last - prev));
            dirty = true;
        }

        if (editPointIdx == -1) { hasGizmoClaim = false; }
        if (hasGizmoClaim && editPointIdx < static_cast<int>(component.controlPoints.size())) {
            auto* transform = registry.try_get<Component::TransformComponent>(entity);
            if (transform) {
                const glm::mat4 view = viewFamily.mainView.currentViewData.view;
                const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
                const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation);
                const glm::mat4 entityMatInv = glm::inverse(entityMat);

                glm::vec3 worldPt = glm::vec3(entityMat * glm::vec4(component.controlPoints[editPointIdx], 1.0f));
                glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldPt);

                ImGuizmo::PushID(editPointIdx);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                    glm::value_ptr(mat))) {
                    component.controlPoints[editPointIdx] = glm::vec3(entityMatInv * glm::vec4(glm::vec3(mat[3]), 1.0f));
                }
                const bool usingGizmo = ImGuizmo::IsUsing();
                ImGuizmo::PopID();
                if (!usingGizmo && wasUsingGizmo) {
                    dirty = true;
                }
                wasUsingGizmo = usingGizmo;
            }
        }

        // Debug: draw control polygon and point spheres
        {
            constexpr glm::vec4 kLineColor{0.3f, 0.7f, 1.0f, 1.0f};
            constexpr glm::vec4 kPointColor{0.5f, 0.9f, 1.0f, 1.0f};
            constexpr glm::vec4 kEditColor{1.0f, 0.7f, 0.1f, 1.0f};
            constexpr float kPointRadius = 0.08f;
            auto* transform = registry.try_get<Component::TransformComponent>(entity);
            const glm::mat4 entityMat = transform
                                            ? glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation)
                                            : glm::mat4(1.0f);

            for (int i = 0; i < static_cast<int>(component.controlPoints.size()); i++) {
                glm::vec3 wp = glm::vec3(entityMat * glm::vec4(component.controlPoints[i], 1.0f));
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {wp, kPointRadius, (i == editPointIdx) ? kEditColor : kPointColor});
                if (i + 1 < static_cast<int>(component.controlPoints.size())) {
                    glm::vec3 wp2 = glm::vec3(entityMat * glm::vec4(component.controlPoints[i + 1], 1.0f));
                    DEBUG_ADD_LINE(viewFamily.debugLines, {wp, wp2, kLineColor});
                }
            }
        }

        // Material selector
        {
            const char* currentLabel = "(none)";
            if (component.material.IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.material)) {
                    currentLabel = m->name.c_str();
                }
            }
            if (ImGui::BeginCombo("Material##spline", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        if (component.bPrimitiveReady) {
                            ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
                            component.bPrimitiveReady = false;
                        }
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<Component::SplineMeshLoadingTag>(entity);
                        state->bPendingModelResolve = true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) continue;
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            if (component.bPrimitiveReady) {
                                ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
                                component.bPrimitiveReady = false;
                            }
                            component.material = matId;
                            registry.emplace_or_replace<Component::SplineMeshLoadingTag>(entity);
                            state->bPendingModelResolve = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (dirty) {
            if (component.modelHandle.IsValid()) {
                ctx->assetManager->UnloadModel(component.modelHandle);
                component.modelHandle = {};
            }
            if (component.bPrimitiveReady) {
                ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
                component.bPrimitiveReady = false;
            }
            Engine::SplineParams params;
            params.controlPoints = component.controlPoints;
            params.radius = component.radius;
            params.rollAngle = component.rollAngle;
            params.sides = component.sides;
            params.segmentsPerSpan = component.segmentsPerSpan;
            params.bClosed = component.bClosed;
            params.bCaps = component.bCaps;
            component.modelHandle = ctx->assetManager->LoadSplineModel(params);
            registry.emplace_or_replace<Component::SplineMeshLoadingTag>(entity);
            state->bPendingModelResolve = true;
        }
    }

    if (hasGizmoClaim) { state->bCustomGizmoActive = true; }

    return {.requestRemoval = remove};
}

template<>
void OnComponentAdded<Component::SplineMeshComponent>(Component::SplineMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();

    Engine::SplineParams params;
    params.controlPoints = component.controlPoints;
    params.radius = component.radius;
    params.rollAngle = component.rollAngle;
    params.sides = component.sides;
    params.segmentsPerSpan = component.segmentsPerSpan;
    params.bClosed = component.bClosed;
    params.bCaps = component.bCaps;

    component.modelHandle = ctx->assetManager->LoadSplineModel(params);
    registry.emplace_or_replace<Component::SplineMeshLoadingTag>(entity);
    state->bPendingModelResolve = true;

    auto* transform = registry.try_get<Component::TransformComponent>(entity);
    glm::mat4 m = transform ? Component::GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
}

template<>
void OnComponentRemoved<Component::SplineMeshComponent>(Component::SplineMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    if (component.bPrimitiveReady) {
        ctx->materialManager->ReleaseMaterial(component.primitive.materialID);
    }
    if (component.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(component.modelHandle);
    }
    registry.remove<Component::SplineMeshLoadingTag>(entity);
    registry.remove<Component::RenderTransformComponent>(entity);
    registry.remove<Component::DirtyRenderTransformComponent>(entity);
    registry.remove<Component::SplineMeshComponent>(entity);
}
}
