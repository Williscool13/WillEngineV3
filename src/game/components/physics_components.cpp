//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include <glm/gtc/type_ptr.hpp>


#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/fwd_components.h"
#include "game/systems/physics_system.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "physics/physics_system.h"

#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void PhysicsBodyComponent::on_construct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity[physics.bodyID] = entity;
}

void PhysicsBodyComponent::on_destroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity.erase(physics.bodyID);
}
}

namespace Game
{
template<>
void SerializeComponent<Component::PhysicsBodyDesc>(const Component::PhysicsBodyDesc& comp, nlohmann::json& json)
{
    json["motionType"] = comp.motionType;
    json["mass"] = comp.mass;
    json["friction"] = comp.friction;
    json["shapes"] = nlohmann::json::array();

    for (const auto& shape : comp.shapes) {
        nlohmann::json shapeJson;
        shapeJson["type"] = shape.type;
        shapeJson["offset"] = {shape.offset.x, shape.offset.y, shape.offset.z};
        shapeJson["rotation"] = {shape.rotation.w, shape.rotation.x, shape.rotation.y, shape.rotation.z};

        switch (shape.type) {
            case Component::PhysicsShapeType::Box:
                shapeJson["halfExtents"] = {shape.box.halfExtents.x, shape.box.halfExtents.y, shape.box.halfExtents.z};
                break;
            case Component::PhysicsShapeType::Sphere:
                shapeJson["radius"] = shape.sphere.radius;
                break;
            case Component::PhysicsShapeType::Capsule:
                shapeJson["radius"] = shape.capsule.radius;
                shapeJson["halfHeight"] = shape.capsule.halfHeight;
                break;
            case Component::PhysicsShapeType::ConvexHull:
            case Component::PhysicsShapeType::TriangleMesh:
                shapeJson["meshSourceModelId"] = shape.meshSourceModelId.id;
                shapeJson["proceduralType"] = shape.proceduralParams.index();
                if (!shape.splineParams.controlPoints.empty()) {
                    nlohmann::json sp;
                    sp["radius"] = shape.splineParams.radius;
                    sp["rollAngle"] = shape.splineParams.rollAngle;
                    sp["sides"] = shape.splineParams.sides;
                    sp["segmentsPerSpan"] = shape.splineParams.segmentsPerSpan;
                    sp["bClosed"] = shape.splineParams.bClosed;
                    sp["bCaps"] = shape.splineParams.bCaps;
                    sp["controlPoints"] = nlohmann::json::array();
                    for (const auto& cp : shape.splineParams.controlPoints) {
                        sp["controlPoints"].push_back({cp.x, cp.y, cp.z});
                    }
                    shapeJson["splineParams"] = sp;
                }
                std::visit([&shapeJson](const auto& p) {
                    using T = std::decay_t<decltype(p)>;
                    if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
                        shapeJson["stepCount"] = p.stepCount;
                        shapeJson["width"] = p.width;
                        shapeJson["totalDepth"] = p.totalDepth;
                        shapeJson["totalHeight"] = p.totalHeight;
                        shapeJson["bSpecifyStepHeight"] = p.bSpecifyStepHeight;
                        shapeJson["stepHeight"] = p.stepHeight;
                        shapeJson["bIsClosed"] = p.bIsClosed;
                    }
                    else if constexpr (std::is_same_v<T, Engine::BoxParams>) {
                        shapeJson["sizeX"] = p.sizeX;
                        shapeJson["sizeY"] = p.sizeY;
                        shapeJson["sizeZ"] = p.sizeZ;
                    }
                    else if constexpr (std::is_same_v<T, Engine::CylinderParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["height"] = p.height;
                        shapeJson["slices"] = p.slices;
                        shapeJson["bCapped"] = p.bCapped;
                    }
                    else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["height"] = p.height;
                        shapeJson["slices"] = p.slices;
                        shapeJson["rings"] = p.rings;
                    }
                    else if constexpr (std::is_same_v<T, Engine::TorusParams>) {
                        shapeJson["ringRadius"] = p.ringRadius;
                        shapeJson["tubeRadius"] = p.tubeRadius;
                        shapeJson["slices"] = p.slices;
                        shapeJson["stacks"] = p.stacks;
                    }
                    else if constexpr (std::is_same_v<T, Engine::ArchParams>) {
                        shapeJson["width"] = p.width;
                        shapeJson["height"] = p.height;
                        shapeJson["depth"] = p.depth;
                        shapeJson["thickness"] = p.thickness;
                        shapeJson["sides"] = p.sides;
                    }
                    else if constexpr (std::is_same_v<T, Engine::WedgeParams>) {
                        shapeJson["sizeX"] = p.sizeX;
                        shapeJson["sizeY"] = p.sizeY;
                        shapeJson["sizeZ"] = p.sizeZ;
                    }
                    else if constexpr (std::is_same_v<T, Engine::ConeParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["height"] = p.height;
                        shapeJson["slices"] = p.slices;
                        shapeJson["bCapped"] = p.bCapped;
                    }
                    else if constexpr (std::is_same_v<T, Engine::DoorParams>) {
                        shapeJson["width"] = p.width;
                        shapeJson["height"] = p.height;
                        shapeJson["depth"] = p.depth;
                        shapeJson["archHeight"] = p.archHeight;
                        shapeJson["gap"] = p.gap;
                        shapeJson["sides"] = p.sides;
                        shapeJson["bHalf"] = p.bHalf;
                        shapeJson["bFlip"] = p.bFlip;
                    }
                    else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
                        shapeJson["sizeX"] = p.sizeX;
                        shapeJson["sizeZ"] = p.sizeZ;
                        shapeJson["tilesX"] = p.tilesX;
                        shapeJson["tilesZ"] = p.tilesZ;
                    }
                    else if constexpr (std::is_same_v<T, Engine::SphereParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["slices"] = p.slices;
                        shapeJson["stacks"] = p.stacks;
                    }
                    else if constexpr (std::is_same_v<T, Engine::SubdividedSphereParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["subdivisions"] = p.subdivisions;
                    }
                    else if constexpr (std::is_same_v<T, Engine::HemisphereParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["slices"] = p.slices;
                        shapeJson["stacks"] = p.stacks;
                    }
                    else if constexpr (std::is_same_v<T, Engine::PipeParams>) {
                        shapeJson["outerRadius"] = p.outerRadius;
                        shapeJson["innerRadius"] = p.innerRadius;
                        shapeJson["height"] = p.height;
                        shapeJson["slices"] = p.slices;
                    }
                    else if constexpr (std::is_same_v<T, Engine::TetrahedronParams> ||
                                       std::is_same_v<T, Engine::OctahedronParams> ||
                                       std::is_same_v<T, Engine::IcosahedronParams> ||
                                       std::is_same_v<T, Engine::DodecahedronParams>) {
                        shapeJson["radius"] = p.radius;
                    }
                    else if constexpr (std::is_same_v<T, Engine::KleinBottleParams>) {
                        shapeJson["scale"] = p.scale;
                        shapeJson["slices"] = p.slices;
                        shapeJson["stacks"] = p.stacks;
                    }
                    else if constexpr (std::is_same_v<T, Engine::TrefoilKnotParams>) {
                        shapeJson["scale"] = p.scale;
                        shapeJson["tubeRadius"] = p.tubeRadius;
                        shapeJson["slices"] = p.slices;
                        shapeJson["stacks"] = p.stacks;
                    }
                }, shape.proceduralParams);
                break;
        }

        json["shapes"].push_back(shapeJson);
    }
}

template<>
void DeserializeComponent<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& comp, const nlohmann::json& json)
{
    comp.motionType = static_cast<Component::PhysicsMotionType>(json["motionType"].get<uint8_t>());
    comp.mass = json["mass"].get<float>();
    comp.friction = json.value<float>("friction", 0.0f);
    comp.shapes.clear();

    for (const auto& shapeJson : json["shapes"]) {
        Component::PhysicsShapeDesc shape{};
        shape.type = static_cast<Component::PhysicsShapeType>(shapeJson["type"].get<uint8_t>());

        auto& o = shapeJson["offset"];
        shape.offset = {o[0].get<float>(), o[1].get<float>(), o[2].get<float>()};

        auto& r = shapeJson["rotation"];
        shape.rotation = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};

        switch (shape.type) {
            case Component::PhysicsShapeType::Box:
            {
                auto& h = shapeJson["halfExtents"];
                shape.box.halfExtents = {h[0].get<float>(), h[1].get<float>(), h[2].get<float>()};
                break;
            }
            case Component::PhysicsShapeType::Sphere:
                shape.sphere.radius = shapeJson["radius"].get<float>();
                break;
            case Component::PhysicsShapeType::Capsule:
                shape.capsule.radius = shapeJson["radius"].get<float>();
                shape.capsule.halfHeight = shapeJson["halfHeight"].get<float>();
                break;
            case Component::PhysicsShapeType::ConvexHull:
            case Component::PhysicsShapeType::TriangleMesh:
                if (shapeJson.contains("meshSourceModelId")) {
                    shape.meshSourceModelId = Engine::ModelID(shapeJson["meshSourceModelId"].get<uint64_t>());
                }
                if (shapeJson.contains("proceduralType")) {
                    const int32_t ptype = shapeJson["proceduralType"].get<int32_t>();
                    if (ptype == 1) {
                        Engine::StaircaseParams p{};
                        p.stepCount = shapeJson["stepCount"].get<int32_t>();
                        p.width = shapeJson["width"].get<float>();
                        p.totalDepth = shapeJson["totalDepth"].get<float>();
                        p.totalHeight = shapeJson["totalHeight"].get<float>();
                        p.bSpecifyStepHeight = shapeJson.value("bSpecifyStepHeight", false);
                        p.stepHeight = shapeJson.value("stepHeight", p.totalHeight / static_cast<float>(std::max(p.stepCount, 1)));
                        p.bIsClosed = shapeJson.value("bIsClosed", true);
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 2) {
                        Engine::BoxParams p{};
                        p.sizeX = shapeJson["sizeX"].get<float>();
                        p.sizeY = shapeJson["sizeY"].get<float>();
                        p.sizeZ = shapeJson["sizeZ"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 3) {
                        Engine::CylinderParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.bCapped = shapeJson["bCapped"].get<bool>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 4) {
                        Engine::CapsuleParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.rings = shapeJson["rings"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 5) {
                        Engine::TorusParams p{};
                        p.ringRadius = shapeJson["ringRadius"].get<float>();
                        p.tubeRadius = shapeJson["tubeRadius"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.stacks = shapeJson["stacks"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 6) {
                        Engine::ArchParams p{};
                        p.width = shapeJson["width"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.depth = shapeJson["depth"].get<float>();
                        p.thickness = shapeJson["thickness"].get<float>();
                        p.sides = shapeJson["sides"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 7) {
                        Engine::WedgeParams p{};
                        p.sizeX = shapeJson["sizeX"].get<float>();
                        p.sizeY = shapeJson["sizeY"].get<float>();
                        p.sizeZ = shapeJson["sizeZ"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 8) {
                        Engine::ConeParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.bCapped = shapeJson["bCapped"].get<bool>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 9) {
                        Engine::DoorParams p{};
                        p.width = shapeJson["width"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.depth = shapeJson["depth"].get<float>();
                        p.archHeight = shapeJson.value("archHeight", 0.5f);
                        p.gap = shapeJson.value("gap", 0.0f);
                        p.sides = shapeJson["sides"].get<int32_t>();
                        p.bHalf = shapeJson["bHalf"].get<bool>();
                        p.bFlip = shapeJson.value("bFlip", false);
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 10) {
                        Engine::PlaneParams p{};
                        p.sizeX = shapeJson["sizeX"].get<float>();
                        p.sizeZ = shapeJson["sizeZ"].get<float>();
                        p.tilesX = shapeJson["tilesX"].get<int32_t>();
                        p.tilesZ = shapeJson["tilesZ"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 11) {
                        Engine::SphereParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.stacks = shapeJson["stacks"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 12) {
                        Engine::SubdividedSphereParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.subdivisions = glm::clamp(shapeJson["subdivisions"].get<int32_t>(), 0, 4);
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 13) {
                        Engine::HemisphereParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.stacks = shapeJson["stacks"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 14) {
                        Engine::PipeParams p{};
                        p.outerRadius = shapeJson["outerRadius"].get<float>();
                        p.innerRadius = shapeJson["innerRadius"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 15) {
                        Engine::TetrahedronParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 16) {
                        Engine::OctahedronParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 17) {
                        Engine::IcosahedronParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 18) {
                        Engine::DodecahedronParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 19) {
                        Engine::KleinBottleParams p{};
                        p.scale = shapeJson["scale"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.stacks = shapeJson["stacks"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 20) {
                        Engine::TrefoilKnotParams p{};
                        p.scale = shapeJson["scale"].get<float>();
                        p.tubeRadius = shapeJson["tubeRadius"].get<float>();
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.stacks = shapeJson["stacks"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                }
                if (shapeJson.contains("splineParams")) {
                    const auto& sp = shapeJson["splineParams"];
                    Engine::SplineParams spline{};
                    spline.radius = sp["radius"].get<float>();
                    spline.rollAngle = sp["rollAngle"].get<float>();
                    spline.sides = sp["sides"].get<int32_t>();
                    spline.segmentsPerSpan = sp["segmentsPerSpan"].get<int32_t>();
                    spline.bClosed = sp["bClosed"].get<bool>();
                    spline.bCaps = sp["bCaps"].get<bool>();
                    spline.controlPoints.clear();
                    for (const auto& cp : sp["controlPoints"]) {
                        spline.controlPoints.push_back({cp[0].get<float>(), cp[1].get<float>(), cp[2].get<float>()});
                    }
                    shape.splineParams = std::move(spline);
                }
                break;
        }

        comp.shapes.push_back(shape);
    }
}

template<>
ComponentEditorResult DrawComponentEditor<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                      const char* name)
{
    static int editShapeIdx = -1;
    static entt::entity editEntity = entt::null;

    auto state = registry.ctx().get<Engine::GameState*>();
    auto ctx = registry.ctx().get<Core::EngineContext*>();

    if (editEntity != entity) {
        editShapeIdx = -1;
        editEntity = entity;
    }

    const bool hasGizmoClaim = editShapeIdx != -1 && !state->bCustomGizmoActive;
    if (hasGizmoClaim) state->bCustomGizmoActive = true;

    bool open = ImGui::CollapsingHeader("Physics Body", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X");
    ImGui::PopStyleColor();

    if (open) {
        const char* motionTypes[] = {"Static", "Kinematic", "Dynamic"};
        int currentMotion = static_cast<int>(component.motionType);
        if (ImGui::Combo("Motion Type", &currentMotion, motionTypes, IM_ARRAYSIZE(motionTypes))) {
            auto newMotion = static_cast<Component::PhysicsMotionType>(currentMotion);
            if (newMotion == Component::PhysicsMotionType::Dynamic) {
                for (auto& shape : component.shapes) {
                    if (shape.type == Component::PhysicsShapeType::TriangleMesh) {
                        if (shape.meshSourceHandle.IsValid()) {
                            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                            shape.meshSourceHandle = {};
                        }
                        shape.meshSourceModelId = Engine::ModelID::INVALID;
                        shape.proceduralParams = std::monostate{};
                        shape.type = Component::PhysicsShapeType::Box;
                    }
                }
            }
            component.motionType = newMotion;
        }

        ImGui::DragFloat("Mass", &component.mass, 0.1f, 0.001f, 10000.0f);
        ImGui::DragFloat("Friction", &component.friction, 0.001f, 0.001f, 1.0f);

        const char* qualityTypes[] = {"Discrete", "LinearCast"};
        int currentQuality = static_cast<int>(component.motionQuality);
        if (ImGui::Combo("Motion Quality", &currentQuality, qualityTypes, IM_ARRAYSIZE(qualityTypes)))
            component.motionQuality = static_cast<JPH::EMotionQuality>(currentQuality);

        const glm::mat4 view = viewFamily.mainView.currentViewData.view;
        const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
        auto* transform = registry.try_get<Component::TransformComponent>(entity);

        auto renderShapeContent = [&](Component::PhysicsShapeDesc& shape) {
            const bool bIsDynamic = component.motionType == Component::PhysicsMotionType::Dynamic;
            static constexpr const char* kShapeTypes[] = {"Box", "Sphere", "Capsule", "ConvexHull", "TriangleMesh"};
            if (ImGui::BeginCombo("Shape Type", kShapeTypes[static_cast<int>(shape.type)])) {
                for (int s = 0; s < IM_ARRAYSIZE(kShapeTypes); ++s) {
                    const bool bDisabled = bIsDynamic && s == static_cast<int>(Component::PhysicsShapeType::TriangleMesh);
                    ImGui::BeginDisabled(bDisabled);
                    if (ImGui::Selectable(kShapeTypes[s], static_cast<int>(shape.type) == s)) {
                        auto newType = static_cast<Component::PhysicsShapeType>(s);
                        bool wasMesh = shape.type == Component::PhysicsShapeType::ConvexHull || shape.type == Component::PhysicsShapeType::TriangleMesh;
                        bool isMesh = newType == Component::PhysicsShapeType::ConvexHull || newType == Component::PhysicsShapeType::TriangleMesh;
                        if (wasMesh && !isMesh && shape.meshSourceHandle.IsValid()) {
                            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                            shape.meshSourceHandle = {};
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                        }
                        shape.type = newType;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat3("Offset", &shape.offset.x, 0.01f);

            switch (shape.type) {
                case Component::PhysicsShapeType::Box:
                    ImGui::DragFloat3("Half Extents", &shape.box.halfExtents.x, 0.01f, 0.001f, 100.0f);
                    break;
                case Component::PhysicsShapeType::Sphere:
                    ImGui::DragFloat("Radius", &shape.sphere.radius, 0.01f, 0.001f, 100.0f);
                    break;
                case Component::PhysicsShapeType::Capsule:
                    ImGui::DragFloat("Radius", &shape.capsule.radius, 0.01f, 0.001f, 100.0f);
                    ImGui::DragFloat("Half Height", &shape.capsule.halfHeight, 0.01f, 0.001f, 100.0f);
                    break;
                case Component::PhysicsShapeType::ConvexHull:
                case Component::PhysicsShapeType::TriangleMesh:
                {
                    const auto* meta = ctx->assetManager->GetModelMetadata(shape.meshSourceModelId);
                    if (meta) {
                        ImGui::Text("Mesh Source: %s", meta->name.c_str());
                    }
                    else {
                        static constexpr const char* kProceduralNames[] = {
                            nullptr, "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch",
                            "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere",
                            "Hemisphere", "Pipe", "Tetrahedron", "Octahedron", "Icosahedron",
                            "Dodecahedron", "Klein Bottle", "Trefoil Knot",
                        };
                        const size_t idx = shape.proceduralParams.index();
                        if (idx > 0 && idx < std::size(kProceduralNames)) {
                            ImGui::Text("Mesh Source: Procedural %s", kProceduralNames[idx]);
                        } else if (!shape.splineParams.controlPoints.empty()) {
                            ImGui::Text("Mesh Source: Procedural Spline");
                        }
                        else {
                            ImGui::Text("Mesh Source: (none)");
                        }
                    }
                    break;
                }
            } {
                const bool isMeshType = shape.type == Component::PhysicsShapeType::ConvexHull || shape.type == Component::PhysicsShapeType::TriangleMesh;

                Engine::StaticModelHandle fitHandle{};
                if (auto* sm = registry.try_get<Component::StaticMeshComponent>(entity))
                    fitHandle = sm->modelHandle;
                else if (auto* pm = registry.try_get<Component::ProceduralMeshComponent>(entity))
                    fitHandle = pm->modelHandle;

                Engine::StaticModel* fitModel = fitHandle.IsValid() ? ctx->assetManager->GetModel(fitHandle) : nullptr;
                const bool bModelLoaded = fitModel && fitModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded;

                if (isMeshType && shape.meshSourceHandle.IsValid()) {
                    const Engine::StaticModel* srcModel = ctx->assetManager->GetModel(shape.meshSourceHandle);
                    if (srcModel && srcModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded
                        && (!srcModel->physicsCache || srcModel->physicsCache->positions.empty())) {
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Nothing will be generated at runtime (vertex count over threshold)");
                    }
                }

                ImGui::BeginDisabled(!bModelLoaded && !isMeshType);
                if (ImGui::Button("Auto-Fit")) {
                    switch (shape.type) {
                        case Component::PhysicsShapeType::Box:
                            shape.box.halfExtents = fitModel->bounds.aabb.HalfExtents();
                            shape.offset = fitModel->bounds.aabb.Center();
                            break;
                        case Component::PhysicsShapeType::Sphere:
                            shape.sphere.radius = fitModel->bounds.sphere.radius;
                            shape.offset = fitModel->bounds.sphere.center;
                            break;
                        case Component::PhysicsShapeType::Capsule:
                        {
                            const glm::vec3 he = fitModel->bounds.aabb.HalfExtents();
                            const float radius = glm::max(he.x, he.z);
                            shape.capsule.radius = radius;
                            shape.capsule.halfHeight = glm::max(0.001f, he.y - radius);
                            shape.offset = fitModel->bounds.aabb.Center();
                            break;
                        }
                        case Component::PhysicsShapeType::ConvexHull:
                        case Component::PhysicsShapeType::TriangleMesh:
                            if (shape.meshSourceHandle.IsValid()) {
                                ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                                shape.meshSourceHandle = {};
                            }
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.proceduralParams = std::monostate{};
                            shape.offset = {};
                            if (auto* sm = registry.try_get<Component::StaticMeshComponent>(entity)) {
                                shape.meshSourceModelId = sm->modelId;
                                shape.meshSourceHandle = ctx->assetManager->LoadModel(shape.meshSourceModelId);
                                registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
                                state->bPendingModelResolve = true;
                            }
                            else if (auto* pm = registry.try_get<Component::ProceduralMeshComponent>(entity)) {
                                shape.proceduralParams = pm->params;
                                shape.meshSourceHandle = ctx->assetManager->LoadProceduralModel(shape.proceduralParams);
                                registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
                                state->bPendingModelResolve = true;
                            } else if (auto* splm = registry.try_get<Component::SplineMeshComponent>(entity)) {
                                shape.splineParams.controlPoints = splm->controlPoints;
                                shape.splineParams.radius = splm->radius;
                                shape.splineParams.rollAngle = splm->rollAngle;
                                shape.splineParams.sides = splm->sides;
                                shape.splineParams.segmentsPerSpan = splm->segmentsPerSpan;
                                shape.splineParams.bClosed = splm->bClosed;
                                shape.splineParams.bCaps = splm->bCaps;
                                shape.meshSourceHandle = ctx->assetManager->LoadSplineModel(shape.splineParams);
                                registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
                                state->bPendingModelResolve = true;
                            }
                            break;
                    }
                }
                ImGui::EndDisabled();
            }
        };

        auto renderGizmo = [&](int i, Component::PhysicsShapeDesc& shape) {
            if (editShapeIdx != i || !transform || !hasGizmoClaim) return;

            const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform->translation) * glm::mat4_cast(transform->rotation);
            const glm::mat4 entityMatInv = glm::inverse(entityMat);
            const glm::vec3 shapeCenter = glm::vec3(entityMat * glm::vec4(shape.offset, 1.0f));

            ImGuizmo::Style savedStyle = ImGuizmo::GetStyle();

            auto applyStyle = [](ImVec4 full, ImVec4 select, float size) {
                ImGuizmo::Style& s = ImGuizmo::GetStyle();
                s.Colors[ImGuizmo::DIRECTION_X] = full;
                s.Colors[ImGuizmo::DIRECTION_Y] = full;
                s.Colors[ImGuizmo::DIRECTION_Z] = full;
                s.Colors[ImGuizmo::PLANE_X] = {full.x, full.y, full.z, 0.38f};
                s.Colors[ImGuizmo::PLANE_Y] = {full.x, full.y, full.z, 0.38f};
                s.Colors[ImGuizmo::PLANE_Z] = {full.x, full.y, full.z, 0.38f};
                s.Colors[ImGuizmo::TRANSLATION_LINE] = full;
                s.Colors[ImGuizmo::SELECTION] = select;
                ImGuizmo::SetGizmoSizeClipSpace(size);
            };

            int gizmoId = 0;
            auto gizmo = [&](glm::vec3 worldPos, auto onMoved) {
                glm::mat4 mat = glm::translate(glm::mat4(1.0f), worldPos);
                ImGuizmo::PushID(gizmoId++);
                if (ImGuizmo::Manipulate(
                    glm::value_ptr(view), glm::value_ptr(proj),
                    ImGuizmo::TRANSLATE, ImGuizmo::WORLD,
                    glm::value_ptr(mat))) {
                    onMoved(glm::vec3(mat[3]));
                }
                ImGuizmo::PopID();
            };

            applyStyle({1.0f, 0.55f, 0.05f, 1.0f}, {1.0f, 0.85f, 0.35f, 1.0f}, 0.10f);
            gizmo(shapeCenter, [&](glm::vec3 newCenter) {
                shape.offset = glm::vec3(entityMatInv * glm::vec4(newCenter, 1.0f));
            });

            applyStyle({1.0f, 0.85f, 0.20f, 1.0f}, {1.0f, 1.00f, 0.60f, 1.0f}, 0.07f);
            switch (shape.type) {
                case Component::PhysicsShapeType::Sphere:
                    gizmo(shapeCenter + glm::vec3(shape.sphere.radius, 0.0f, 0.0f), [&](glm::vec3 newPt) {
                        shape.sphere.radius = glm::max(0.001f, glm::length(newPt - shapeCenter));
                    });
                    break;
                case Component::PhysicsShapeType::Capsule:
                    gizmo(shapeCenter + glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f), [&](glm::vec3 newPt) {
                        shape.capsule.halfHeight = glm::max(0.001f, newPt.y - shapeCenter.y);
                    });
                    gizmo(shapeCenter + glm::vec3(shape.capsule.radius, 0.0f, 0.0f), [&](glm::vec3 newPt) {
                        shape.capsule.radius = glm::max(0.001f, glm::length(newPt - shapeCenter));
                    });
                    break;
                case Component::PhysicsShapeType::Box:
                    gizmo(shapeCenter + glm::vec3(shape.box.halfExtents.x, 0.0f, 0.0f), [&](glm::vec3 newPt) {
                        shape.box.halfExtents.x = glm::max(0.001f, glm::abs(newPt.x - shapeCenter.x));
                    });
                    gizmo(shapeCenter + glm::vec3(0.0f, shape.box.halfExtents.y, 0.0f), [&](glm::vec3 newPt) {
                        shape.box.halfExtents.y = glm::max(0.001f, glm::abs(newPt.y - shapeCenter.y));
                    });
                    gizmo(shapeCenter + glm::vec3(0.0f, 0.0f, shape.box.halfExtents.z), [&](glm::vec3 newPt) {
                        shape.box.halfExtents.z = glm::max(0.001f, glm::abs(newPt.z - shapeCenter.z));
                    });
                    break;
                default:
                    break;
            }

            ImGuizmo::GetStyle() = savedStyle;
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);

            constexpr glm::vec4 kEditColor{1.0f, 0.6f, 0.1f, 1.0f};
            switch (shape.type) {
                case Component::PhysicsShapeType::Sphere:
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {shapeCenter, shape.sphere.radius, kEditColor});
                    break;
                case Component::PhysicsShapeType::Capsule:
                {
                    const glm::vec3 top = shapeCenter + glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                    const glm::vec3 bot = shapeCenter - glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {top, shape.capsule.radius, kEditColor});
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {bot, shape.capsule.radius, kEditColor});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + glm::vec3( shape.capsule.radius, 0, 0), bot + glm::vec3( shape.capsule.radius, 0, 0), kEditColor});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + glm::vec3(-shape.capsule.radius, 0, 0), bot + glm::vec3(-shape.capsule.radius, 0, 0), kEditColor});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + glm::vec3(0, 0, shape.capsule.radius), bot + glm::vec3(0, 0, shape.capsule.radius), kEditColor});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + glm::vec3(0, 0, -shape.capsule.radius), bot + glm::vec3(0, 0, -shape.capsule.radius), kEditColor});
                    break;
                }
                case Component::PhysicsShapeType::Box:
                    DEBUG_ADD_BOX(viewFamily.debugBoxes, {shapeCenter, shape.box.halfExtents, transform->rotation * shape.rotation, kEditColor});
                    break;
            }
        };

        ImGui::SeparatorText("Shape"); {
            auto& shape = component.shapes[0];
            const bool isEditing = (editShapeIdx == 0);
            ImGui::PushID(0);
            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? ImVec4(0.15f, 0.65f, 0.15f, 1.0f) : ImVec4(0.15f, 0.35f, 0.65f, 1.0f));
            ImGui::BeginDisabled((state->bCustomGizmoActive || state->bCustomGizmoActivePrev) && !isEditing);
            if (ImGui::Button(isEditing ? "Done" : "Edit")) {
                editShapeIdx = isEditing ? -1 : 0;
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            renderShapeContent(shape);
            renderGizmo(0, shape);
            ImGui::PopID();
        }

        if (component.shapes.size() > 1) {
            ImGui::SeparatorText("Additional Colliders");
            int shapeToRemove = -1;
            for (int i = 1; i < static_cast<int>(component.shapes.size()); ++i) {
                ImGui::PushID(i);
                auto& shape = component.shapes[i];
                const bool isEditing = (editShapeIdx == i);

                bool shapeOpen = ImGui::TreeNodeEx("", ImGuiTreeNodeFlags_AllowOverlap, "Shape %d", i);
                const float avail = ImGui::GetContentRegionAvail().x;
                const float xBtnW = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float editBtnW = ImGui::CalcTextSize("Done").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;

                ImGui::SameLine(avail - xBtnW - spacing - editBtnW);
                ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? ImVec4(0.15f, 0.65f, 0.15f, 1.0f) : ImVec4(0.15f, 0.35f, 0.65f, 1.0f));
                ImGui::BeginDisabled((state->bCustomGizmoActive || state->bCustomGizmoActivePrev) && !isEditing);
                if (ImGui::SmallButton(isEditing ? "Done##edit" : "Edit##edit")) {
                    editShapeIdx = isEditing ? -1 : i;
                }
                ImGui::EndDisabled();
                ImGui::PopStyleColor();

                ImGui::SameLine(avail - xBtnW);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                if (ImGui::SmallButton("X##shape")) {
                    shapeToRemove = i;
                    if (editShapeIdx == i) { editShapeIdx = -1; }
                }
                ImGui::PopStyleColor();

                if (shapeOpen) {
                    renderShapeContent(shape);
                    ImGui::TreePop();
                }
                renderGizmo(i, shape);
                ImGui::PopID();
            }
            if (shapeToRemove >= 0) {
                component.shapes.erase(component.shapes.begin() + shapeToRemove);
            }
        }

        if (ImGui::Button("Add Collider")) {
            Component::PhysicsShapeDesc desc{};
            desc.type = Component::PhysicsShapeType::Box;
            desc.box.halfExtents = glm::vec3(0.5f);
            component.shapes.push_back(desc);
        }
    }

    return {.requestRemoval = remove};
}

template<>
void OnComponentAdded<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    if (component.shapes.empty()) {
        Component::PhysicsShapeDesc d{};
        d.type = Component::PhysicsShapeType::Box;
        d.box.halfExtents = glm::vec3(0.5f);
        component.shapes.push_back(d);
    }

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::GameState*>();
    bool needsResolve = false;
    for (auto& shape : component.shapes) {
        if (shape.type != Component::PhysicsShapeType::ConvexHull && shape.type != Component::PhysicsShapeType::TriangleMesh) { continue; }
        if (shape.meshSourceModelId.IsValid()) {
            shape.meshSourceHandle = ctx->assetManager->LoadModel(shape.meshSourceModelId);
            needsResolve = true;
        }
        else if (!std::holds_alternative<std::monostate>(shape.proceduralParams)) {
            shape.meshSourceHandle = ctx->assetManager->LoadProceduralModel(shape.proceduralParams);
            needsResolve = true;
        } else if (!shape.splineParams.controlPoints.empty()) {
            shape.meshSourceHandle = ctx->assetManager->LoadSplineModel(shape.splineParams);
            needsResolve = true;
        }
    }
    if (needsResolve) {
        registry.emplace_or_replace<Component::PendingPhysicsMeshTag>(entity);
        state->bPendingModelResolve = true;
    }
}

template<>
void OnComponentRemoved<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    for (auto& shape : component.shapes) {
        if (shape.meshSourceHandle.IsValid()) {
            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
            shape.meshSourceHandle = {};
        }
    }
    registry.remove<Component::PhysicsBodyDesc>(entity);
}

template<>
void OnPlayStart<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    registry.emplace_or_replace<Component::PendingPhysicsBodyCreationTag>(entity);
}

template<>
void OnPlayStop<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<Component::PendingPhysicsBodyCreationTag>(entity);

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    if (auto* body = registry.try_get<Component::PhysicsBodyComponent>(entity)) {
        if (!body->bodyID.IsInvalid()) {
            bodyInterface.RemoveBody(body->bodyID);
            bodyInterface.DestroyBody(body->bodyID);
            registry.remove<Component::PhysicsBodyComponent>(entity);
            registry.remove<Component::DynamicPhysicsBodyComponent>(entity);
        }
    }
}

template<>
ComponentEditorResult DrawComponentEditor<Component::DrawPhysicsDebugTag>(Component::DrawPhysicsDebugTag& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                          const char* name)
{
    ImGui::CollapsingHeader("Physics Debug Draw", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletephysicscomponent");
    ImGui::PopStyleColor();

    return {.requestRemoval = remove};
}
}
