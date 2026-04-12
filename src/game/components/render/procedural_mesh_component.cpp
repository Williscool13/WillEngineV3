//
// Created by William on 2026-03-21.
//

#include "procedural_mesh_component.h"

#include <json/nlohmann/json.hpp>

#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"

namespace Game::Component
{
void ProceduralMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<ProceduralMeshComponent>(entity);
    RecreateProceduralMesh(component, registry, entity);
}

void ProceduralMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<ProceduralMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
    registry.remove<DirtyRenderTransformComponent>(entity);
}

void RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get_or_emplace<MeshRuntime>(entity);

    // Teardown
    for (size_t i = 0; i < runtime.primitives.Size(); ++i) {
        ctx->materialManager->ReleaseMaterial(runtime.primitives[i].materialID);
    }
    runtime.primitives.Clear();
    if (runtime.modelHandle.IsValid()) {
        ctx->assetManager->UnloadModel(runtime.modelHandle);
        runtime.modelHandle = {};
    }

    // Rebuild
    if (!std::holds_alternative<std::monostate>(component.params)) {
        runtime.modelHandle = ctx->assetManager->LoadProceduralModel(component.params);
        registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
        state->bPendingModelResolve |= true;
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    registry.emplace_or_replace<DirtyRenderTransformComponent>(entity);
}
}

namespace Game
{
bool Component::ProceduralMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::SplineMeshComponent>(entity);
}

void Component::ProceduralMeshComponent::Serialize(const ProceduralMeshComponent& comp, nlohmann::json& json)
{
    json["type"] = comp.params.index();
    json["material"] = comp.material.id;
    json["modelFlags"] = {comp.modelFlags.x, comp.modelFlags.y, comp.modelFlags.z, comp.modelFlags.w};

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
        else if constexpr (std::is_same_v<T, Engine::CurvedRampParams>) {
            json["width"] = p.width;
            json["height"] = p.height;
            json["radius"] = p.radius;
            json["segments"] = p.segments;
            json["bHalfPipe"] = p.bHalfPipe;
            json["flatLength"] = p.flatLength;
            json["lipHeight"] = p.lipHeight;
        }
        else if constexpr (std::is_same_v<T, Engine::BowlParams>) {
            json["radius"] = p.radius;
            json["height"] = p.height;
            json["curveRadius"] = p.curveRadius;
            json["flatRadius"] = p.flatRadius;
            json["lipHeight"] = p.lipHeight;
            json["slices"] = p.slices;
            json["segments"] = p.segments;
        }
    }, comp.params);
}

void Component::ProceduralMeshComponent::Deserialize(ProceduralMeshComponent& comp, const nlohmann::json& json)
{
    comp.material = Engine::MaterialID(json["material"].get<uint64_t>());
    if (json.contains("modelFlags")) {
        const auto& f = json["modelFlags"];
        comp.modelFlags = glm::vec4(f[0].get<float>(), f[1].get<float>(), f[2].get<float>(), f[3].get<float>());
    }

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
    else if (type == 21) {
        Engine::CurvedRampParams p{};
        p.width = json["width"].get<float>();
        p.height = json["height"].get<float>();
        p.radius = json["radius"].get<float>();
        p.segments = json["segments"].get<int32_t>();
        p.bHalfPipe = json.value("bHalfPipe", false);
        p.flatLength = json.value("flatLength", 1.0f);
        p.lipHeight = json.value("lipHeight", 0.02f);
        comp.params = p;
    }
    else if (type == 22) {
        Engine::BowlParams p{};
        p.radius = json["radius"].get<float>();
        p.height = json["height"].get<float>();
        p.curveRadius = json["curveRadius"].get<float>();
        p.flatRadius = json.value("flatRadius", 0.0f);
        p.lipHeight = json.value("lipHeight", 0.02f);
        p.slices = json["slices"].get<int32_t>();
        p.segments = json["segments"].get<int32_t>();
        comp.params = p;
    }
}

Engine::ComponentEditorResult Component::ProceduralMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                              entt::entity entity, const char* name)
{
    auto& component = registry.get<ProceduralMeshComponent>(entity);
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
        auto* state = registry.ctx().get<Engine::EngineState*>();

        if (std::holds_alternative<std::monostate>(component.params)) {
            if (ImGui::BeginCombo("Shape", "")) {
                auto selectShape = [&](auto&& params) {
                    component.params = std::move(params);
                    RecreateProceduralMesh(component, registry, entity);
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
                if (ImGui::Selectable("Curved Ramp")) selectShape(Engine::CurvedRampParams{});
                if (ImGui::Selectable("Bowl")) selectShape(Engine::BowlParams{});
                ImGui::EndCombo();
            }
        }
        else {
            static constexpr const char* shapeNames[] = {
                "", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
                "Icosahedron", "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl"
            };
            ImGui::Text("Shape: %s", shapeNames[component.params.index()]);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##deselect_shape")) {
                component.params = std::monostate{};
                RecreateProceduralMesh(component, registry, entity);
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
                else if constexpr (std::is_same_v<T, Engine::CurvedRampParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Segments", &p.segments, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Half-Pipe", &p.bHalfPipe)) { dirty = true; }
                    if (p.bHalfPipe) {
                        ImGui::DragFloat("Flat Length", &p.flatLength, 0.01f, 0.0f, 100.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    ImGui::DragFloat("Lip Height", &p.lipHeight, 0.005f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::BowlParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Curve Radius", &p.curveRadius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Flat Radius", &p.flatRadius, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Lip Height", &p.lipHeight, 0.005f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Segments", &p.segments, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }, component.params);

            if (dirty) {
                RecreateProceduralMesh(component, registry, entity);
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
            auto* runtime = registry.try_get<MeshRuntime>(entity);
            if (ImGui::BeginCombo("Material", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        if (runtime && !runtime->primitives.IsEmpty()) {
                            ctx->materialManager->ReleaseMaterial(runtime->primitives[0].materialID);
                            runtime->primitives.Clear();
                        }
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
                        state->bPendingModelResolve |= true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) continue;
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            if (runtime && !runtime->primitives.IsEmpty()) {
                                ctx->materialManager->ReleaseMaterial(runtime->primitives[0].materialID);
                                runtime->primitives.Clear();
                            }
                            component.material = matId;
                            registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
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

} // Game