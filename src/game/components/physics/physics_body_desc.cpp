//
// Created by William on 2026-03-21.
//

#include "physics_body_desc.h"

#include <glm/gtc/type_ptr.hpp>
#include <json/nlohmann/json.hpp>

#include "physics_body_component.h"
#include "physics_components.h"
#include "physics_shape_helpers.h"
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/resources/physics/collider_generation.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/text3d_component.h"

namespace Game::Component
{
static void ApplyRenderTransform(PhysicsShapeDesc& shape, const glm::vec3& scale, const glm::vec3& renderOffset, const glm::quat& renderRotation)
{
    shape.offset = scale * renderOffset + renderRotation * shape.offset;
    shape.rotation = renderRotation * shape.rotation;
}

static bool IsMeshShapeType(PhysicsShapeType type)
{
    return type == PhysicsShapeType::Collider;
}

// A concave collider source: a non-analytic procedural (Klein Bottle, Trefoil Knot, Bowl, Curved Ramp) or a precise Text3D. Only a triangle mesh, so it cannot back a dynamic body.
static bool ShapeIsConcaveExotic(const PhysicsShapeDesc& shape)
{
    if (!IsMeshShapeType(shape.type)) { return false; }
    if (!std::holds_alternative<std::monostate>(shape.proceduralParams)) {
        return !Engine::CanBuildProceduralCollider(shape.proceduralParams);
    }
    if (shape.meshSourceModelId.IsValid()) {
        return shape.bMeshPrecise;
    }
    return shape.text3DSource.IsValid() && shape.text3DSource.bPrecise;
}

static bool BodyHasConcaveExotic(const PhysicsBodyDesc& component)
{
    for (const auto& shape : component.shapes) {
        if (ShapeIsConcaveExotic(shape)) { return true; }
    }
    return false;
}

void PhysicsBodyDesc::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    // Default shape automatically fits to procedurals (static meshes just get box)
    if (component.shapes.IsEmpty()) {
        auto* transform = registry.try_get<TransformComponent>(entity);
        const glm::vec3 scale = transform ? transform->scale : glm::vec3(1.0f);

        if (auto* sm = registry.try_get<StaticMeshComponent>(entity); sm && sm->modelId.IsValid()) {
            PhysicsShapeDesc box{};
            box.type = PhysicsShapeType::Box;
            box.box.halfExtents = glm::vec3(0.5f);
            const auto* meta = ctx->assetManager->GetModelMetadata(sm->modelId);
            if (meta && meta->bounds.aabb.min.x <= meta->bounds.aabb.max.x) {
                box.box.halfExtents = meta->bounds.aabb.HalfExtents() * scale;
                box.offset = meta->bounds.aabb.Center() * scale;
            }
            ApplyRenderTransform(box, scale, sm->renderOffset, sm->renderRotation);
            component.shapes.PushBack(box);
        }
        else if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
            PhysicsShapeDesc shape = MakeProceduralShape(pm->params, scale);
            ApplyRenderTransform(shape, scale, pm->renderOffset, pm->renderRotation);
            component.shapes.PushBack(shape);
        }
        else if (auto* splm = registry.try_get<SplineMeshComponent>(entity); splm && !splm->spline.points.IsEmpty()) {
            PhysicsShapeDesc s{};
            s.type = PhysicsShapeType::Collider;
            s.bakedScale = scale;
            FillSplineParams(s.splineParams, *splm);
            component.shapes.PushBack(s);
        }
        else {
            PhysicsShapeDesc d{};
            d.type = PhysicsShapeType::Box;
            d.box.halfExtents = glm::vec3(0.5f);
            component.shapes.PushBack(d);
        }
    }

    // Source loaded (freeze-gated) in PhysicsMeshPendingKickoff so a model hot-reload can release this body's ref and re-acquire after the drain.
    bool bHasMeshShape = false;
    for (const auto& shape : component.shapes) {
        if (shape.type == PhysicsShapeType::Collider) {
            bHasMeshShape = true;
            break;
        }
    }
    if (bHasMeshShape) {
        registry.remove<PhysicsMeshLoadingTag>(entity);
        registry.emplace_or_replace<PendingPhysicsMeshTag>(entity);
        state->bPendingModelResolve = true;
    }

    registry.emplace_or_replace<PendingPhysicsShapeCreationTag>(entity);
    registry.emplace_or_replace<PendingPhysicsBodyCreationTag>(entity);
}

void PhysicsBodyDesc::OnUpdate(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    for (auto& shape : component.shapes) {
        if (shape.colliderHandle.IsValid()) {
            ctx->assetManager->UnloadCollider(shape.colliderHandle);
            shape.colliderHandle = {};
        }
    }
    OnConstruct(registry, entity);
}

void PhysicsBodyDesc::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto& component = registry.get<PhysicsBodyDesc>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    for (auto& shape : component.shapes) {
        if (shape.colliderHandle.IsValid()) {
            ctx->assetManager->UnloadCollider(shape.colliderHandle);
            shape.colliderHandle = {};
        }
    }
    registry.remove<PhysicsBodyComponent>(entity);
    registry.remove<DynamicPhysicsBodyComponent>(entity);
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
            case Component::PhysicsShapeType::Collider:
                shapeJson["meshSourceModelId"] = shape.meshSourceModelId.id;
                shapeJson["meshPrecise"] = shape.bMeshPrecise;
                shapeJson["proceduralType"] = shape.proceduralParams.index();
                if (!shape.splineParams.spline.points.IsEmpty()) {
                    nlohmann::json sp;
                    Engine::Spline::Serialize(shape.splineParams.spline, sp["spline"]);
                    sp["radius"] = shape.splineParams.radius;
                    sp["rollAngle"] = shape.splineParams.rollAngle;
                    sp["sides"] = shape.splineParams.sides;
                    sp["segmentsPerSpan"] = shape.splineParams.segmentsPerSpan;
                    sp["bCaps"] = shape.splineParams.bCaps;
                    sp["bCrossPlanks"] = shape.splineParams.bCrossPlanks;
                    sp["crossPlankInterval"] = shape.splineParams.crossPlankInterval;
                    sp["crossPlankHeight"] = shape.splineParams.crossPlankHeight;
                    sp["crossPlankThickness"] = shape.splineParams.crossPlankThickness;
                    sp["crossPlankLength"] = shape.splineParams.crossPlankLength;
                    sp["profileType"] = static_cast<int32_t>(shape.splineParams.profile.type);
                    sp["profileWidth"] = shape.splineParams.profile.width;
                    sp["profileHeight"] = shape.splineParams.profile.height;
                    sp["profileCornerRadius"] = shape.splineParams.profile.cornerRadius;
                    sp["profileCornerSegments"] = shape.splineParams.profile.cornerSegments;
                    sp["profileThickness"] = shape.splineParams.profile.thickness;
                    sp["railingEnabled"] = shape.splineParams.railing.bEnabled;
                    sp["railingPosts"] = shape.splineParams.railing.bPosts;
                    sp["railingPostInterval"] = shape.splineParams.railing.postInterval;
                    sp["railingPostBottom"] = shape.splineParams.railing.postBottom;
                    sp["railingPostTop"] = shape.splineParams.railing.postTop;
                    sp["railingPostSizeX"] = shape.splineParams.railing.postSize.x;
                    sp["railingPostSizeY"] = shape.splineParams.railing.postSize.y;
                    sp["railingPostLateral"] = shape.splineParams.railing.postLateral;
                    sp["railingLateralOffset"] = shape.splineParams.railing.lateralOffset;
                    auto& laneArr = sp["railingLanes"] = nlohmann::json::array();
                    for (int li = 0; li < static_cast<int>(shape.splineParams.railing.lanes.Size()); li++) {
                        laneArr.push_back({shape.splineParams.railing.lanes[li].x, shape.splineParams.railing.lanes[li].y});
                    }
                    shapeJson["splineParams"] = sp;
                }
                if (shape.text3DSource.IsValid()) {
                    nlohmann::json t3;
                    t3["fontId"] = shape.text3DSource.fontId.id;
                    t3["text"] = shape.text3DSource.text.c_str();
                    t3["depth"] = shape.text3DSource.depth;
                    t3["flatness"] = shape.text3DSource.flatness;
                    t3["tracking"] = shape.text3DSource.tracking;
                    t3["scale"] = shape.text3DSource.scale;
                    t3["smoothNormals"] = shape.text3DSource.bSmoothNormals;
                    t3["precise"] = shape.text3DSource.bPrecise;
                    shapeJson["text3DSource"] = t3;
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
                        shapeJson["bFillCorners"] = p.bFillCorners;
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
                    else if constexpr (std::is_same_v<T, Engine::SpiralStaircaseParams>) {
                        shapeJson["stepCount"] = p.stepCount;
                        shapeJson["stepHeight"] = p.stepHeight;
                        shapeJson["totalHeight"] = p.totalHeight;
                        shapeJson["bSpecifyStepHeight"] = p.bSpecifyStepHeight;
                        shapeJson["outerRadius"] = p.outerRadius;
                        shapeJson["centerColumnRadius"] = p.centerColumnRadius;
                        shapeJson["treadThickness"] = p.treadThickness;
                        shapeJson["degreesPerStep"] = p.degreesPerStep;
                        shapeJson["totalSweep"] = p.totalSweep;
                        shapeJson["bSpecifyDegreesPerStep"] = p.bSpecifyDegreesPerStep;
                        shapeJson["arcSegments"] = p.arcSegments;
                        shapeJson["bShowCenterColumn"] = p.bShowCenterColumn;
                        shapeJson["bRamp"] = p.bRamp;
                    }
                }, shape.proceduralParams);
                break;
        }

        json["shapes"].push_back(shapeJson);
    }
}

void Component::PhysicsBodyDesc::Deserialize(PhysicsBodyDesc& comp, const nlohmann::json& json)
{
    comp.motionType = static_cast<PhysicsMotionType>(json["motionType"].get<uint8_t>());
    comp.mass = json["mass"].get<float>();
    comp.friction = json.value<float>("friction", 0.0f);
    comp.restitution = json.value<float>("restitution", 0.0f);
    comp.motionQuality = static_cast<JPH::EMotionQuality>(json.value<uint8_t>("motionQuality", 0));
    comp.layerOverride = json.value<JPH::ObjectLayer>("layerOverride", 0xFFFF);
    comp.bEnhancedInternalEdgeRemoval = json.value<bool>("enhancedInternalEdgeRemoval", false);
    comp.bIsSensor = json.value<bool>("isSensor", false);
    comp.shapes.Clear();

    for (const auto& shapeJson : json["shapes"]) {
        PhysicsShapeDesc shape{};
        // Legacy migration: ConvexHull(3)/TriangleMesh(4)/Compound(5) collapsed into Collider(3)
        const uint8_t rawType = shapeJson["type"].get<uint8_t>();
        shape.type = rawType >= static_cast<uint8_t>(PhysicsShapeType::Collider) ? PhysicsShapeType::Collider : static_cast<PhysicsShapeType>(rawType);

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
            case PhysicsShapeType::Box:
            {
                auto& h = shapeJson["halfExtents"];
                shape.box.halfExtents = {h[0].get<float>(), h[1].get<float>(), h[2].get<float>()};
                break;
            }
            case PhysicsShapeType::Sphere:
                shape.sphere.radius = shapeJson["radius"].get<float>();
                break;
            case PhysicsShapeType::Capsule:
                shape.capsule.radius = shapeJson["radius"].get<float>();
                shape.capsule.halfHeight = shapeJson["halfHeight"].get<float>();
                break;
            case PhysicsShapeType::Collider:
                if (shapeJson.contains("meshSourceModelId")) {
                    shape.meshSourceModelId = Engine::ModelID(shapeJson["meshSourceModelId"].get<uint64_t>());
                }
                shape.bMeshPrecise = shapeJson.value("meshPrecise", false);
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
                        p.bFillCorners = shapeJson.value("bFillCorners", false);
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
                    else if (ptype == 23) {
                        Engine::SpiralStaircaseParams p{};
                        p.stepCount = shapeJson["stepCount"].get<int32_t>();
                        p.stepHeight = shapeJson["stepHeight"].get<float>();
                        p.totalHeight = shapeJson.value("totalHeight", p.stepHeight * static_cast<float>(std::max(p.stepCount, 1)));
                        p.bSpecifyStepHeight = shapeJson.value("bSpecifyStepHeight", false);
                        p.outerRadius = shapeJson["outerRadius"].get<float>();
                        p.centerColumnRadius = shapeJson["centerColumnRadius"].get<float>();
                        p.treadThickness = shapeJson.value("treadThickness", 0.08f);
                        p.degreesPerStep = shapeJson.value("degreesPerStep", 30.0f);
                        p.totalSweep = shapeJson.value("totalSweep", p.degreesPerStep * static_cast<float>(std::max(p.stepCount, 1)));
                        p.bSpecifyDegreesPerStep = shapeJson.value("bSpecifyDegreesPerStep", false);
                        p.arcSegments = shapeJson.value("arcSegments", 6);
                        p.bShowCenterColumn = shapeJson.value("bShowCenterColumn", true);
                        p.bRamp = shapeJson.value("bRamp", false);
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
                    spline.bCrossPlanks = sp.value("bCrossPlanks", false);
                    spline.crossPlankInterval = sp.value("crossPlankInterval", 4);
                    spline.crossPlankHeight = sp.value("crossPlankHeight", 0.0f);
                    spline.crossPlankThickness = sp.value("crossPlankThickness", 0.1f);
                    spline.crossPlankLength = sp.value("crossPlankLength", 0.3f);
                    spline.profile.type = static_cast<Engine::SplineProfileType>(sp.value("profileType", 0));
                    spline.profile.width = sp.value("profileWidth", 0.4f);
                    spline.profile.height = sp.value("profileHeight", 0.4f);
                    spline.profile.cornerRadius = sp.value("profileCornerRadius", 0.08f);
                    spline.profile.cornerSegments = sp.value("profileCornerSegments", 3);
                    spline.profile.thickness = sp.value("profileThickness", 0.05f);
                    spline.railing.bEnabled = sp.value("railingEnabled", false);
                    spline.railing.bPosts = sp.value("railingPosts", true);
                    spline.railing.postInterval = sp.value("railingPostInterval", 4);
                    spline.railing.postBottom = sp.value("railingPostBottom", 0.0f);
                    spline.railing.postTop = sp.value("railingPostTop", 1.0f);
                    spline.railing.postSize.x = sp.value("railingPostSizeX", 0.05f);
                    spline.railing.postSize.y = sp.value("railingPostSizeY", 0.05f);
                    spline.railing.postLateral = sp.value("railingPostLateral", 0.0f);
                    spline.railing.lateralOffset = sp.value("railingLateralOffset", 0.0f);
                    spline.railing.lanes.Clear();
                    if (sp.contains("railingLanes")) {
                        for (const auto& e : sp["railingLanes"]) {
                            if (spline.railing.lanes.Size() >= 8) { break; }
                            spline.railing.lanes.PushBack(Vec2{e[0].get<float>(), e[1].get<float>()});
                        }
                    }
                    shape.splineParams = spline;
                }
                if (shapeJson.contains("text3DSource")) {
                    const auto& t3 = shapeJson["text3DSource"];
                    Text3DShapeSource src{};
                    src.fontId = Engine::FontID(t3["fontId"].get<uint64_t>());
                    src.text = Core::InlineString<256>(t3["text"].get<std::string>().c_str());
                    src.depth = t3.value("depth", 0.2f);
                    src.flatness = t3.value("flatness", 0.005f);
                    src.tracking = t3.value("tracking", 0.0f);
                    src.scale = t3.value("scale", 1.0f);
                    src.bSmoothNormals = t3.value("smoothNormals", true);
                    src.bPrecise = t3.value("precise", false);
                    shape.text3DSource = src;
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
    static bool bGizmoWasDragging = false;

    auto state = registry.ctx().get<Engine::EngineState*>();
    auto ctx = registry.ctx().get<Engine::EngineContext*>();

    if (editEntity != entity) {
        editShapeIdx = -1;
        editEntity = entity;
        bGizmoWasDragging = false;
    }

    const bool hasGizmoClaim = editShapeIdx != -1;
    if (hasGizmoClaim) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Physics Body", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, Editor::BUTTON_TRANSPAREN);
    bool remove = ImGui::SmallButton("X");
    ImGui::PopStyleColor();

    if (open) {
        const bool bForbidDynamic = BodyHasConcaveExotic(component);
        const char* motionTypes[] = {"Static", "Kinematic", "Dynamic"};
        int currentMotion = static_cast<int>(component.motionType);
        if (ImGui::BeginCombo("Motion Type", motionTypes[currentMotion])) {
            for (int m = 0; m < IM_ARRAYSIZE(motionTypes); ++m) {
                const bool bDisabled = bForbidDynamic && m == static_cast<int>(PhysicsMotionType::Dynamic);
                ImGui::BeginDisabled(bDisabled);
                if (ImGui::Selectable(motionTypes[m], currentMotion == m)) {
                    const auto newMotion = static_cast<PhysicsMotionType>(m);
                    if (newMotion != component.motionType) {
                        component.motionType = newMotion;
                        registry.patch<PhysicsBodyDesc>(entity);
                    }
                }
                if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Concave procedural shapes (Klein Bottle, Trefoil Knot, Bowl, Curved Ramp) can only be Static or Kinematic.");
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
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
            const glm::vec3 scale = transform ? transform->scale : glm::vec3(1.0f);

            bool bRenderSourceExotic = false;
            if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
                bRenderSourceExotic = !std::holds_alternative<std::monostate>(pm->params) && !Engine::CanBuildProceduralCollider(pm->params);
            }

            Engine::StaticModelHandle fitHandle{};
            if (auto* rt = registry.try_get<MeshRuntime>(entity)) {
                fitHandle = rt->modelHandle;
            }
            Engine::StaticModel* fitModel = fitHandle.IsValid() ? ctx->assetManager->GetModel(fitHandle) : nullptr;
            const bool bModelLoaded = fitModel && fitModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded;

            static constexpr const char* kShapeTypes[] = {"Box", "Sphere", "Capsule", "Collider"};
            if (ImGui::BeginCombo("Shape Type", kShapeTypes[static_cast<int>(shape.type)])) {
                for (int s = 0; s < IM_ARRAYSIZE(kShapeTypes); ++s) {
                    const auto candidate = static_cast<PhysicsShapeType>(s);
                    // Exotics are concave and cannot be dynamic
                    const bool bDisabled = bIsDynamic && candidate == PhysicsShapeType::Collider && bRenderSourceExotic;
                    ImGui::BeginDisabled(bDisabled);
                    if (ImGui::Selectable(kShapeTypes[s], static_cast<int>(shape.type) == s)) {
                        auto newType = candidate;
                        bool wasMesh = IsMeshShapeType(shape.type);
                        bool isMesh = IsMeshShapeType(newType);
                        if (wasMesh && !isMesh) {
                            if (shape.colliderHandle.IsValid()) {
                                ctx->assetManager->UnloadCollider(shape.colliderHandle);
                                shape.colliderHandle = {};
                            }
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.splineParams.spline.points.Clear();
                            shape.text3DSource = {};
                        }

                        shape.type = newType;
                        if (wasMesh && !isMesh) {
                            if (bModelLoaded) {
                                FitPrimitiveShapeToEntity(registry, entity, shape, scale, fitModel->bounds);
                            }
                            else {
                                shape.offset = glm::vec3(0.0f);
                                shape.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                                shape.bakedScale = glm::vec3(1.0f);
                                switch (newType) {
                                    case PhysicsShapeType::Box:
                                        shape.box.halfExtents = glm::vec3(0.5f);
                                        break;
                                    case PhysicsShapeType::Sphere:
                                        shape.sphere.radius = 0.5f;
                                        break;
                                    case PhysicsShapeType::Capsule:
                                        shape.capsule.radius = 0.5f;
                                        shape.capsule.halfHeight = 0.5f;
                                        break;
                                    default:
                                        break;
                                }
                            }
                        }
                        else if (!wasMesh && isMesh) {
                            FitMeshShapeToEntity(registry, entity, shape, scale);
                        }
                        registry.patch<PhysicsBodyDesc>(entity);
                    }
                    if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("This concave procedural source only has a triangle-mesh collider; switch the body to Static or Kinematic to use it.");
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
                case PhysicsShapeType::Collider:
                {
                    bool bHasAny = false;
                    const auto* meta = ctx->assetManager->GetModelMetadata(shape.meshSourceModelId);
                    static constexpr Core::Array<const char*, 24> kProceduralNames = {
                        nullptr, "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch",
                        "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere",
                        "Hemisphere", "Pipe", "Tetrahedron", "Octahedron", "Icosahedron",
                        "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl", "Spiral Staircase",
                    };
                    const size_t idx = shape.proceduralParams.index();

                    if (meta) {
                        ImGui::Text("Mesh Source: %s", meta->name.c_str());
                        bHasAny = true;

                        ImGui::BeginDisabled(bIsDynamic);
                        if (ImGui::Checkbox("Precise (Triangle Mesh)", &shape.bMeshPrecise)) {
                            if (shape.colliderHandle.IsValid()) {
                                ctx->assetManager->UnloadCollider(shape.colliderHandle);
                                shape.colliderHandle = {};
                            }
                            bAnyChange = true;
                        }
                        if (bIsDynamic && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("A precise mesh collider is concave and cannot back a dynamic body.");
                        }
                        ImGui::EndDisabled();
                    }
                    else if (idx > 0 && idx < kProceduralNames.Size()) {
                        ImGui::Text("Mesh Source: Procedural %s", kProceduralNames[idx]);
                        bHasAny = true;
                    }
                    else if (!shape.splineParams.spline.points.IsEmpty()) {
                        ImGui::Text("Mesh Source: Procedural Spline");
                        bHasAny = true;
                    }
                    else if (shape.text3DSource.IsValid()) {
                        ImGui::Text("Mesh Source: 3D Text");
                        bHasAny = true;

                        ImGui::BeginDisabled(bIsDynamic);
                        if (ImGui::Checkbox("Precise (Triangle Mesh)", &shape.text3DSource.bPrecise)) {
                            if (shape.colliderHandle.IsValid()) {
                                ctx->assetManager->UnloadCollider(shape.colliderHandle);
                                shape.colliderHandle = {};
                            }
                            bAnyChange = true;
                        }
                        if (bIsDynamic && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("A precise text collider is a concave triangle mesh and cannot back a dynamic body.");
                        }
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::Text("Mesh Source: (none)");
                    }


                    if (bHasAny) {
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
                        ImGui::PushStyleColor(ImGuiCol_Button, Editor::BUTTON_TRANSPAREN);
                        const bool bShouldClearMesh = ImGui::SmallButton("X");
                        ImGui::PopStyleColor();
                        if (bShouldClearMesh) {
                            if (shape.colliderHandle.IsValid()) {
                                ctx->assetManager->UnloadCollider(shape.colliderHandle);
                                shape.colliderHandle = {};
                            }
                            shape.meshSourceModelId = Engine::ModelID::INVALID;
                            shape.proceduralParams = std::monostate{};
                            shape.splineParams.spline.points.Clear();
                            shape.text3DSource = {};
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
                const bool isMeshType = IsMeshShapeType(shape.type);

                ImGui::BeginDisabled(!bModelLoaded && !isMeshType);
                if (ImGui::Button("Auto-Fit")) {
                    if (isMeshType) {
                        FitMeshShapeToEntity(registry, entity, shape, scale);
                    }
                    else {
                        FitPrimitiveShapeToEntity(registry, entity, shape, scale, fitModel->bounds);
                    }
                    registry.patch<PhysicsBodyDesc>(entity);
                }
                ImGui::EndDisabled();
            }
        };

        auto renderGizmo = [&](int i, PhysicsShapeDesc& shape) {
            if (editShapeIdx != i || !transform || !hasGizmoClaim) return;

            const auto& world = registry.get<WorldTransformComponent>(entity);
            const Mat4 entityMat = glm::translate(Mat4(1.0f), world.translation) * glm::mat4_cast(world.rotation);
            const Mat4 entityMatInv = glm::inverse(entityMat);
            const Vec3 shapeCenter = Vec3(entityMat * Vec4(shape.offset, 1.0f));
            const Vec3 entityRight = world.rotation * Vec3(1.0f, 0.0f, 0.0f);
            const Vec3 entityUp = world.rotation * Vec3(0.0f, 1.0f, 0.0f);
            const Vec3 entityForward = world.rotation * Vec3(0.0f, 0.0f, 1.0f);
            const auto& vd = viewFamily.mainView.currentViewData;

            auto* ctx = registry.ctx().get<Engine::EngineContext*>();
            const Vec4 viewport{
                static_cast<float>(ctx->windowContext.viewportOffsetX),
                static_cast<float>(ctx->windowContext.viewportOffsetY),
                static_cast<float>(ctx->windowContext.viewportWidth),
                static_cast<float>(ctx->windowContext.viewportHeight),
            };

            constexpr ImU32 colorX = Editor::COLOR_AXIS_X;
            constexpr ImU32 colorY = Editor::COLOR_AXIS_Y;
            constexpr ImU32 colorZ = Editor::COLOR_AXIS_Z;

            // Handles run before the offset gizmo so they win overlapping clicks.
            bool bHandleBusy = false;
            switch (shape.type) {
                case PhysicsShapeType::Sphere:
                {
                    const Vec3 planeNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                    bHandleBusy |= Editor::DotHandle(10000, shapeCenter + entityRight * shape.sphere.radius, planeNormal,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.sphere.radius = glm::max(0.001f, glm::length(newPt - shapeCenter)); },
                                                     colorX);
                    break;
                }
                case PhysicsShapeType::Capsule:
                {
                    const Vec3 upPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityUp) * entityUp);
                    const Vec3 rightPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                    bHandleBusy |= Editor::DotHandle(10000, shapeCenter + entityUp * shape.capsule.halfHeight, upPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.capsule.halfHeight = glm::max(0.001f, glm::dot(newPt - shapeCenter, entityUp)); },
                                                     colorY);
                    bHandleBusy |= Editor::DotHandle(10001, shapeCenter + entityRight * shape.capsule.radius, rightPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.capsule.radius = glm::max(0.001f, glm::length(newPt - shapeCenter)); },
                                                     colorX);
                    break;
                }
                case PhysicsShapeType::Box:
                {
                    const Vec3 xPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                    const Vec3 yPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityUp) * entityUp);
                    const Vec3 zPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityForward) * entityForward);
                    bHandleBusy |= Editor::DotHandle(10000, shapeCenter + entityRight * shape.box.halfExtents.x, xPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.box.halfExtents.x = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityRight))); },
                                                     colorX);
                    bHandleBusy |= Editor::DotHandle(10001, shapeCenter + entityUp * shape.box.halfExtents.y, yPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.box.halfExtents.y = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityUp))); },
                                                     colorY);
                    bHandleBusy |= Editor::DotHandle(10002, shapeCenter + entityForward * shape.box.halfExtents.z, zPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.box.halfExtents.z = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityForward))); },
                                                     colorZ);
                    break;
                }
                default:
                    break;
            }

            if (!bHandleBusy && state->editor.activeDotHandleId == -1) {
                ImGuizmo::SetGizmoSizeClipSpace(0.10f);
                ImGuizmo::PushID(0);
                Mat4 mat = glm::translate(Mat4(1.0f), shapeCenter);
                if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), ImGuizmo::TRANSLATE, ImGuizmo::WORLD, glm::value_ptr(mat))) {
                    shape.offset = Vec3(entityMatInv * Vec4(Vec3(mat[3]), 1.0f));
                }
                if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
                ImGuizmo::PopID();
                ImGuizmo::SetGizmoSizeClipSpace(0.1f);
            }

            // Rebuild the shape/body once on drag release rather than every frame of the drag.
            const bool bGizmoDragging = state->editor.activeDotHandleId != -1 || ImGuizmo::IsUsing();
            if (bGizmoWasDragging && !bGizmoDragging) {
                registry.patch<PhysicsBodyDesc>(entity);
            }
            bGizmoWasDragging = bGizmoDragging;

            constexpr Vec4 editColorX = Editor::DEBUG_AXIS_X;
            constexpr Vec4 editColorY = Editor::DEBUG_AXIS_Y;
            constexpr Vec4 editColorZ = Editor::DEBUG_AXIS_Z;
            switch (shape.type) {
                case PhysicsShapeType::Sphere:
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {shapeCenter, shape.sphere.radius, editColorX});
                    break;
                case PhysicsShapeType::Capsule:
                {
                    const Vec3 top = shapeCenter + entityUp * shape.capsule.halfHeight;
                    const Vec3 bot = shapeCenter - entityUp * shape.capsule.halfHeight;
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {top, shape.capsule.radius, editColorY});
                    DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {bot, shape.capsule.radius, editColorY});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + entityRight * shape.capsule.radius, bot + entityRight * shape.capsule.radius, editColorX});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top - entityRight * shape.capsule.radius, bot - entityRight * shape.capsule.radius, editColorX});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top + entityForward * shape.capsule.radius, bot + entityForward * shape.capsule.radius, editColorZ});
                    DEBUG_ADD_LINE(viewFamily.debugLines, {top - entityForward * shape.capsule.radius, bot - entityForward * shape.capsule.radius, editColorZ});
                    break;
                }
                case PhysicsShapeType::Box:
                {
                    const Quat boxRot = transform->rotation * shape.rotation;
                    const Vec3 bx = boxRot * Vec3(shape.box.halfExtents.x, 0.0f, 0.0f);
                    const Vec3 by = boxRot * Vec3(0.0f, shape.box.halfExtents.y, 0.0f);
                    const Vec3 bz = boxRot * Vec3(0.0f, 0.0f, shape.box.halfExtents.z);
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + bx, shape.box.halfExtents.y, shape.box.halfExtents.z, glm::normalize(by), glm::normalize(bz), editColorX, 0.02f});
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - bx, shape.box.halfExtents.y, shape.box.halfExtents.z, glm::normalize(by), glm::normalize(bz), editColorX, 0.02f});
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + by, shape.box.halfExtents.x, shape.box.halfExtents.z, glm::normalize(bx), glm::normalize(bz), editColorY, 0.02f});
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - by, shape.box.halfExtents.x, shape.box.halfExtents.z, glm::normalize(bx), glm::normalize(bz), editColorY, 0.02f});
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + bz, shape.box.halfExtents.x, shape.box.halfExtents.y, glm::normalize(bx), glm::normalize(by), editColorZ, 0.02f});
                    DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - bz, shape.box.halfExtents.x, shape.box.halfExtents.y, glm::normalize(bx), glm::normalize(by), editColorZ, 0.02f});
                    break;
                }
            }
        };

        ImGui::SeparatorText("Shape"); {
            auto& shape = component.shapes[0];
            const bool isEditing = (editShapeIdx == 0);
            ImGui::PushID(0);
            ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
            ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !isEditing);
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
                ImGui::PushStyleColor(ImGuiCol_Button, isEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
                ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !isEditing);
                if (ImGui::SmallButton(isEditing ? "Done##edit" : "Edit##edit")) {
                    editShapeIdx = isEditing ? -1 : i;
                }
                ImGui::EndDisabled();
                ImGui::PopStyleColor();

                ImGui::SameLine(avail - xBtnW);
                ImGui::PushStyleColor(ImGuiCol_Button, Editor::BUTTON_TRANSPAREN);
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
