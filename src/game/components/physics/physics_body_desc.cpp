//
// Created by William on 2026-03-21.
//

#include "physics_body_desc.h"

#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "physics_components.h"
#include "game/component-registry/component_editor.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"

namespace Game::Component
{
void PhysicsBodyDesc::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);

    if (component.shapes.IsEmpty()) {
        PhysicsShapeDesc d{};
        d.type = PhysicsShapeType::Box;
        d.box.halfExtents = glm::vec3(0.5f);
        component.shapes.PushBack(d);
    }

    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();
    bool needsResolve = false;
    for (auto& shape : component.shapes) {
        if (shape.type != PhysicsShapeType::ConvexHull && shape.type != PhysicsShapeType::TriangleMesh) { continue; }
        if (shape.meshSourceModelId.IsValid()) {
            shape.meshSourceHandle = ctx->assetManager->LoadModel(shape.meshSourceModelId);
            needsResolve = true;
        }
        else if (!std::holds_alternative<std::monostate>(shape.proceduralParams)) {
            shape.meshSourceHandle = ctx->assetManager->LoadProceduralModel(shape.proceduralParams);
            needsResolve = true;
        }
        else if (!shape.splineParams.spline.points.IsEmpty()) {
            shape.meshSourceHandle = ctx->assetManager->LoadSplineModel(shape.splineParams);
            needsResolve = true;
        }
    }
    if (needsResolve) {
        registry.emplace_or_replace<PendingPhysicsMeshTag>(entity);
        state->bPendingModelResolve = true;
    }

    registry.emplace_or_replace<PendingPhysicsShapeCreationTag>(entity);
}

void PhysicsBodyDesc::OnUpdate(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    for (auto& shape : component.shapes) {
        if (shape.meshSourceHandle.IsValid()) {
            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
            shape.meshSourceHandle = {};
        }
    }
    OnConstruct(registry, entity);
}

void PhysicsBodyDesc::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    for (auto& shape : component.shapes) {
        if (shape.meshSourceHandle.IsValid()) {
            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
            shape.meshSourceHandle = {};
        }
    }
}
}


namespace Game
{
void Component::PhysicsBodyDesc::Serialize(const PhysicsBodyDesc& comp, nlohmann::json& json)
{
    json["motionType"] = comp.motionType;
    json["mass"] = comp.mass;
    json["friction"] = comp.friction;
    json["restitution"] = comp.restitution;
    json["motionQuality"] = static_cast<uint8_t>(comp.motionQuality);
    json["layerOverride"] = comp.layerOverride;
    json["enhancedInternalEdgeRemoval"] = comp.bEnhancedInternalEdgeRemoval;
    json["isSensor"] = comp.bIsSensor;
    json["shapes"] = nlohmann::json::array();

    for (const auto& shape : comp.shapes) {
        nlohmann::json shapeJson;
        shapeJson["type"] = shape.type;
        shapeJson["offset"] = {shape.offset.x, shape.offset.y, shape.offset.z};
        shapeJson["rotation"] = {shape.rotation.w, shape.rotation.x, shape.rotation.y, shape.rotation.z};
        shapeJson["bakedScaleX"] = shape.bakedScale.x;
        shapeJson["bakedScaleY"] = shape.bakedScale.y;
        shapeJson["bakedScaleZ"] = shape.bakedScale.z;

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
                if (!shape.splineParams.spline.points.IsEmpty()) {
                    nlohmann::json sp;
                    Engine::Spline::Serialize(shape.splineParams.spline, sp["spline"]);
                    sp["radius"] = shape.splineParams.radius;
                    sp["rollAngle"] = shape.splineParams.rollAngle;
                    sp["sides"] = shape.splineParams.sides;
                    sp["segmentsPerSpan"] = shape.splineParams.segmentsPerSpan;
                    sp["bCaps"] = shape.splineParams.bCaps;
                    sp["bDualPath"] = shape.splineParams.bDualPath;
                    sp["dualPathSpacing"] = shape.splineParams.dualPathSpacing;
                    sp["bCrossPlanks"] = shape.splineParams.bCrossPlanks;
                    sp["crossPlankInterval"] = shape.splineParams.crossPlankInterval;
                    sp["crossPlankHeight"] = shape.splineParams.crossPlankHeight;
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
                    else if constexpr (std::is_same_v<T, Engine::CurvedRampParams>) {
                        shapeJson["width"] = p.width;
                        shapeJson["height"] = p.height;
                        shapeJson["radius"] = p.radius;
                        shapeJson["segments"] = p.segments;
                        shapeJson["bHalfPipe"] = p.bHalfPipe;
                        shapeJson["flatLength"] = p.flatLength;
                        shapeJson["lipHeight"] = p.lipHeight;
                    }
                    else if constexpr (std::is_same_v<T, Engine::BowlParams>) {
                        shapeJson["radius"] = p.radius;
                        shapeJson["height"] = p.height;
                        shapeJson["curveRadius"] = p.curveRadius;
                        shapeJson["flatRadius"] = p.flatRadius;
                        shapeJson["lipHeight"] = p.lipHeight;
                        shapeJson["slices"] = p.slices;
                        shapeJson["segments"] = p.segments;
                    }
                }, shape.proceduralParams);
                break;
        }

        json["shapes"].push_back(shapeJson);
    }
}

void Component::PhysicsBodyDesc::Deserialize(PhysicsBodyDesc& comp, const nlohmann::json& json)
{
    comp.motionType = static_cast<Component::PhysicsMotionType>(json["motionType"].get<uint8_t>());
    comp.mass = json["mass"].get<float>();
    comp.friction = json.value<float>("friction", 0.0f);
    comp.restitution = json.value<float>("restitution", 0.0f);
    comp.motionQuality = static_cast<JPH::EMotionQuality>(json.value<uint8_t>("motionQuality", 0));
    comp.layerOverride = json.value<JPH::ObjectLayer>("layerOverride", 0xFFFF);
    comp.bEnhancedInternalEdgeRemoval = json.value<bool>("enhancedInternalEdgeRemoval", false);
    comp.bIsSensor = json.value<bool>("isSensor", false);
    comp.shapes.Clear();

    for (const auto& shapeJson : json["shapes"]) {
        Component::PhysicsShapeDesc shape{};
        shape.type = static_cast<Component::PhysicsShapeType>(shapeJson["type"].get<uint8_t>());

        auto& o = shapeJson["offset"];
        shape.offset = {o[0].get<float>(), o[1].get<float>(), o[2].get<float>()};

        auto& r = shapeJson["rotation"];
        shape.rotation = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};

        shape.bakedScale = {
            shapeJson.value("bakedScaleX", 1.0f),
            shapeJson.value("bakedScaleY", 1.0f),
            shapeJson.value("bakedScaleZ", 1.0f)
        };

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
                    else if (ptype == 21) {
                        Engine::CurvedRampParams p{};
                        p.width = shapeJson["width"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.radius = shapeJson["radius"].get<float>();
                        p.segments = shapeJson["segments"].get<int32_t>();
                        p.bHalfPipe = shapeJson.value("bHalfPipe", false);
                        p.flatLength = shapeJson.value("flatLength", 1.0f);
                        p.lipHeight = shapeJson.value("lipHeight", 0.02f);
                        shape.proceduralParams = p;
                    }
                    else if (ptype == 22) {
                        Engine::BowlParams p{};
                        p.radius = shapeJson["radius"].get<float>();
                        p.height = shapeJson["height"].get<float>();
                        p.curveRadius = shapeJson["curveRadius"].get<float>();
                        p.flatRadius = shapeJson.value("flatRadius", 0.0f);
                        p.lipHeight = shapeJson.value("lipHeight", 0.02f);
                        p.slices = shapeJson["slices"].get<int32_t>();
                        p.segments = shapeJson["segments"].get<int32_t>();
                        shape.proceduralParams = p;
                    }
                }
                if (shapeJson.contains("splineParams")) {
                    const auto& sp = shapeJson["splineParams"];
                    Engine::SplineParams spline{};
                    if (sp.contains("spline")) { Engine::Spline::Deserialize(spline.spline, sp["spline"]); }
                    spline.radius = sp.value("radius", 0.5f);
                    spline.rollAngle = sp.value("rollAngle", 0.0f);
                    spline.sides = sp.value("sides", 8);
                    spline.segmentsPerSpan = sp.value("segmentsPerSpan", 8);
                    spline.bCaps = sp.value("bCaps", true);
                    spline.bDualPath = sp.value("bDualPath", false);
                    spline.dualPathSpacing = sp.value("dualPathSpacing", 1.0f);
                    spline.bCrossPlanks = sp.value("bCrossPlanks", false);
                    spline.crossPlankInterval = sp.value("crossPlankInterval", 4);
                    spline.crossPlankHeight = sp.value("crossPlankHeight", 0.0f);
                    shape.splineParams = spline;
                }
                break;
        }

        comp.shapes.PushBack(shape);
    }
}

Engine::ComponentEditorResult Component::PhysicsBodyDesc::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                                      const char* name)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    static int editShapeIdx = -1;
    static entt::entity editEntity = entt::null;

    auto state = registry.ctx().get<Engine::EngineState*>();
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
            auto newMotion = static_cast<PhysicsMotionType>(currentMotion);
            if (newMotion == PhysicsMotionType::Dynamic) {
                for (auto& shape : component.shapes) {
                    if (shape.type == PhysicsShapeType::TriangleMesh) {
                        if (shape.meshSourceHandle.IsValid()) {
                            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                            shape.meshSourceHandle = {};
                        }
                        shape.meshSourceModelId = Engine::ModelID::INVALID;
                        shape.proceduralParams = std::monostate{};
                        shape.splineParams.spline.points.Clear();
                        shape.type = PhysicsShapeType::Box;
                    }
                }
            }
            component.motionType = newMotion;
        }

        ImGui::DragFloat("Mass", &component.mass, 0.1f, 0.001f, 10000.0f);
        ImGui::DragFloat("Friction", &component.friction, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Restitution", &component.restitution, 0.01f, 0.0f, 1.0f);

        const char* qualityTypes[] = {"Discrete", "LinearCast"};
        int currentQuality = static_cast<int>(component.motionQuality);
        if (ImGui::Combo("Motion Quality", &currentQuality, qualityTypes, IM_ARRAYSIZE(qualityTypes)))
            component.motionQuality = static_cast<JPH::EMotionQuality>(currentQuality);

        int layer = static_cast<int>(component.layerOverride);
        if (layer == 0xFFFF) layer = -1;
        if (ImGui::InputInt("Layer Override", &layer)) {
            component.layerOverride = layer < 0 ? JPH::ObjectLayer(0xFFFF) : JPH::ObjectLayer(layer);
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("-1 = auto (derived from motion type)"); }

        ImGui::Checkbox("Enhanced Internal Edge Removal", &component.bEnhancedInternalEdgeRemoval);
        ImGui::Checkbox("Is Sensor", &component.bIsSensor);

        const glm::mat4 view = viewFamily.mainView.currentViewData.view;
        const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
        auto* transform = registry.try_get<TransformComponent>(entity);

        auto renderShapeContent = [&](PhysicsShapeDesc& shape) {
            const bool bIsDynamic = component.motionType == PhysicsMotionType::Dynamic;
            static constexpr const char* kShapeTypes[] = {"Box", "Sphere", "Capsule", "ConvexHull", "TriangleMesh"};
            if (ImGui::BeginCombo("Shape Type", kShapeTypes[static_cast<int>(shape.type)])) {
                for (int s = 0; s < IM_ARRAYSIZE(kShapeTypes); ++s) {
                    const bool bDisabled = bIsDynamic && s == static_cast<int>(PhysicsShapeType::TriangleMesh);
                    ImGui::BeginDisabled(bDisabled);
                    if (ImGui::Selectable(kShapeTypes[s], static_cast<int>(shape.type) == s)) {
                        auto newType = static_cast<PhysicsShapeType>(s);
                        bool wasMesh = shape.type == PhysicsShapeType::ConvexHull || shape.type == PhysicsShapeType::TriangleMesh;
                        bool isMesh = newType == PhysicsShapeType::ConvexHull || newType == PhysicsShapeType::TriangleMesh;
                        if (wasMesh && !isMesh && shape.meshSourceHandle.IsValid()) {
                            ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                            shape.meshSourceHandle = {};
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.splineParams.spline.points.Clear();
                        }
                        shape.type = newType;
                        registry.patch<PhysicsBodyDesc>(entity);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat3("Offset", &shape.offset.x, 0.01f);
            ImGui::DragFloat3("Baked Scale", &shape.bakedScale.x, 0.01f, 0.001f, 100.0f);

            bool bAnyChange = false;
            switch (shape.type) {
                case PhysicsShapeType::Box:
                    bAnyChange |= ImGui::DragFloat3("Half Extents", &shape.box.halfExtents.x, 0.01f, 0.001f, 100.0f);
                    break;
                case PhysicsShapeType::Sphere:
                    bAnyChange |= ImGui::DragFloat("Radius", &shape.sphere.radius, 0.01f, 0.001f, 100.0f);
                    break;
                case PhysicsShapeType::Capsule:
                    bAnyChange |= ImGui::DragFloat("Radius", &shape.capsule.radius, 0.01f, 0.001f, 100.0f);
                    bAnyChange |= ImGui::DragFloat("Half Height", &shape.capsule.halfHeight, 0.01f, 0.001f, 100.0f);
                    break;
                case PhysicsShapeType::ConvexHull:
                case PhysicsShapeType::TriangleMesh:
                {
                    bool bHasAny = false;
                    const auto* meta = ctx->assetManager->GetModelMetadata(shape.meshSourceModelId);
                    static constexpr Core::Array<const char*, 23> kProceduralNames = {
                        nullptr, "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch",
                        "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere",
                        "Hemisphere", "Pipe", "Tetrahedron", "Octahedron", "Icosahedron",
                        "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl",
                    };
                    const size_t idx = shape.proceduralParams.index();

                    if (meta) {
                        ImGui::Text("Mesh Source: %s", meta->name.c_str());
                        bHasAny = true;
                    }
                    else if (idx > 0 && idx < kProceduralNames.Size()) {
                        ImGui::Text("Mesh Source: Procedural %s", kProceduralNames[idx]);
                        bHasAny = true;
                    }
                    else if (!shape.splineParams.spline.points.IsEmpty()) {
                        ImGui::Text("Mesh Source: Procedural Spline");
                        bHasAny = true;
                    }
                    else {
                        ImGui::Text("Mesh Source: (none)");
                    }


                    if (bHasAny) {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        const bool bShouldClearMesh = ImGui::SmallButton("X");
                        ImGui::PopStyleColor();
                        if (bShouldClearMesh) {
                            if (shape.meshSourceHandle.IsValid()) {
                                ctx->assetManager->UnloadModel(shape.meshSourceHandle);
                                shape.meshSourceHandle = {};
                            }
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.proceduralParams = std::monostate{};
                            shape.splineParams.spline.points.Clear();
                            bAnyChange = true;
                        }
                    }
                    break;
                }
            }
            if (bAnyChange) {
                registry.patch<PhysicsBodyDesc>(entity);
            }

            //
            {
                const bool isMeshType = shape.type == PhysicsShapeType::ConvexHull || shape.type == PhysicsShapeType::TriangleMesh;

                Engine::StaticModelHandle fitHandle{};
                if (auto* rt = registry.try_get<MeshRuntime>(entity)) {
                    fitHandle = rt->modelHandle;
                }

                Engine::StaticModel* fitModel = fitHandle.IsValid() ? ctx->assetManager->GetModel(fitHandle) : nullptr;
                const bool bModelLoaded = fitModel && fitModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded;


                ImGui::BeginDisabled(!bModelLoaded && !isMeshType);
                if (ImGui::Button("Auto-Fit")) {
                    const glm::vec3 scale = transform ? transform->scale : glm::vec3(1.0f);
                    switch (shape.type) {
                        case PhysicsShapeType::Box:
                            shape.box.halfExtents = fitModel->bounds.aabb.HalfExtents() * scale;
                            shape.offset = fitModel->bounds.aabb.Center() * scale;
                            break;
                        case PhysicsShapeType::Sphere:
                        {
                            const float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
                            shape.sphere.radius = fitModel->bounds.sphere.radius * maxScale;
                            shape.offset = fitModel->bounds.sphere.center * scale;
                            break;
                        }
                        case PhysicsShapeType::Capsule:
                        {
                            const glm::vec3 he = fitModel->bounds.aabb.HalfExtents() * scale;
                            const float radius = glm::max(he.x, he.z);
                            shape.capsule.radius = radius;
                            shape.capsule.halfHeight = glm::max(0.001f, he.y - radius);
                            shape.offset = fitModel->bounds.aabb.Center() * scale;
                            break;
                        }
                        case PhysicsShapeType::ConvexHull:
                        case PhysicsShapeType::TriangleMesh:
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.proceduralParams = std::monostate{};
                            shape.splineParams.spline.points.Clear();
                            shape.offset = {};
                            shape.bakedScale = scale;
                            if (auto* sm = registry.try_get<StaticMeshComponent>(entity)) {
                                shape.meshSourceModelId = sm->modelId;
                            }
                            else if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
                                shape.proceduralParams = pm->params;
                            }
                            else if (auto* splm = registry.try_get<SplineMeshComponent>(entity)) {
                                shape.splineParams.spline = splm->spline;
                                shape.splineParams.radius = splm->radius;
                                shape.splineParams.rollAngle = splm->rollAngle;
                                shape.splineParams.sides = splm->sides;
                                shape.splineParams.segmentsPerSpan = splm->segmentsPerSpan;
                                shape.splineParams.bCaps = splm->bCaps;
                                shape.splineParams.bDualPath = splm->bDualPath;
                                shape.splineParams.dualPathSpacing = splm->dualPathSpacing;
                                shape.splineParams.bCrossPlanks = splm->bCrossPlanks;
                                shape.splineParams.crossPlankInterval = splm->crossPlankInterval;
                                shape.splineParams.crossPlankHeight = splm->crossPlankHeight;
                            }
                            break;
                    }

                    registry.patch<PhysicsBodyDesc>(entity);
                }
                ImGui::EndDisabled();
            }
        };

        auto renderGizmo = [&](int i, PhysicsShapeDesc& shape) {
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
                case PhysicsShapeType::Sphere:
                    gizmo(shapeCenter + glm::vec3(shape.sphere.radius, 0.0f, 0.0f), [&](glm::vec3 newPt) {
                        shape.sphere.radius = glm::max(0.001f, glm::length(newPt - shapeCenter));
                    });
                    break;
                case PhysicsShapeType::Capsule:
                    gizmo(shapeCenter + glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f), [&](glm::vec3 newPt) {
                        shape.capsule.halfHeight = glm::max(0.001f, newPt.y - shapeCenter.y);
                    });
                    gizmo(shapeCenter + glm::vec3(shape.capsule.radius, 0.0f, 0.0f), [&](glm::vec3 newPt) {
                        shape.capsule.radius = glm::max(0.001f, glm::length(newPt - shapeCenter));
                    });
                    break;
                case PhysicsShapeType::Box:
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
                case PhysicsShapeType::Sphere:
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {shapeCenter, shape.sphere.radius, kEditColor});
                    break;
                case PhysicsShapeType::Capsule:
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
                case PhysicsShapeType::Box:
                    DEBUG_ADD_BOX(viewFamily.debugBoxes, {shapeCenter, shape.box.halfExtents, transform->rotation * shape.rotation, kEditColor});
                    break;
            }
        };

        ImGui::SeparatorText("Shape");
        {
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

        if (component.shapes.Size() > 1) {
            ImGui::SeparatorText("Additional Colliders");
            int shapeToRemove = -1;
            for (int i = 1; i < static_cast<int>(component.shapes.Size()); ++i) {
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
                component.shapes.RemoveAt(shapeToRemove);
                registry.patch<PhysicsBodyDesc>(entity);
            }
        }

        if (ImGui::Button("Add Collider")) {
            PhysicsShapeDesc desc{};
            desc.type = PhysicsShapeType::Box;
            desc.box.halfExtents = glm::vec3(0.5f);
            component.shapes.PushBack(desc);
            registry.patch<PhysicsBodyDesc>(entity);
        }
    }

    return {.requestRemoval = remove};
}
}
