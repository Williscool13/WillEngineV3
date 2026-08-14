//
// Created by William on 2026-03-21.
//

#include "physics_body_desc.h"

#include <glm/gtc/type_ptr.hpp>

#include "physics_body_component.h"
#include "physics_components.h"
#include "physics_shape_helpers.h"
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/resources/physics/collider_generation.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
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
        state->assetLoad.bPendingModelResolve = true;
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
void Component::PhysicsBodyDesc::Serialize(const PhysicsBodyDesc& comp, Engine::TextWriter& w)
{
    w.Key("motionType", static_cast<uint32_t>(comp.motionType));
    w.Key("mass", comp.mass);
    w.Key("friction", comp.friction);
    w.Key("restitution", comp.restitution);
    w.Key("motionQuality", static_cast<uint32_t>(comp.motionQuality));
    w.Key("layerOverride", static_cast<uint32_t>(comp.layerOverride));
    w.Key("enhancedInternalEdgeRemoval", comp.bEnhancedInternalEdgeRemoval);
    w.Key("isSensor", comp.bIsSensor);

    if (comp.shapes.IsEmpty()) {
        return;
    }
    w.Count("shapes", static_cast<uint32_t>(comp.shapes.Size()));
    for (const auto& shape : comp.shapes) {
        w.BeginBlock("shape");
        w.Key("type", static_cast<uint32_t>(shape.type));
        w.Key("offset", shape.offset);
        w.Key("rotation", shape.rotation);
        w.Key("bakedScale", shape.bakedScale);

        switch (shape.type) {
            case Component::PhysicsShapeType::Box:
                w.Key("halfExtents", shape.box.halfExtents);
                break;
            case Component::PhysicsShapeType::Sphere:
                w.Key("radius", shape.sphere.radius);
                break;
            case Component::PhysicsShapeType::Capsule:
                w.Key("radius", shape.capsule.radius);
                w.Key("halfHeight", shape.capsule.halfHeight);
                break;
            case Component::PhysicsShapeType::Collider:
                w.Key("meshSourceModelId", shape.meshSourceModelId.id);
                w.Key("meshPrecise", shape.bMeshPrecise);
                w.Key("proceduralType", static_cast<uint32_t>(shape.proceduralParams.index()));
                if (!shape.splineParams.spline.points.IsEmpty()) {
                    w.BeginBlock("splineParams");
                    w.BeginBlock("spline");
                    Engine::Spline::Serialize(shape.splineParams.spline, w);
                    w.EndBlock();
                    w.Key("radius", shape.splineParams.radius);
                    w.Key("rollAngle", shape.splineParams.rollAngle);
                    w.Key("sides", shape.splineParams.sides);
                    w.Key("segmentsPerSpan", shape.splineParams.segmentsPerSpan);
                    w.Key("bCaps", shape.splineParams.bCaps);
                    w.Key("bCrossPlanks", shape.splineParams.bCrossPlanks);
                    w.Key("crossPlankInterval", shape.splineParams.crossPlankInterval);
                    w.Key("crossPlankHeight", shape.splineParams.crossPlankHeight);
                    w.Key("crossPlankThickness", shape.splineParams.crossPlankThickness);
                    w.Key("crossPlankLength", shape.splineParams.crossPlankLength);
                    w.Key("profileType", static_cast<int32_t>(shape.splineParams.profile.type));
                    w.Key("profileWidth", shape.splineParams.profile.width);
                    w.Key("profileHeight", shape.splineParams.profile.height);
                    w.Key("profileCornerRadius", shape.splineParams.profile.cornerRadius);
                    w.Key("profileCornerSegments", shape.splineParams.profile.cornerSegments);
                    w.Key("profileThickness", shape.splineParams.profile.thickness);
                    w.Key("railingEnabled", shape.splineParams.railing.bEnabled);
                    w.Key("railingPosts", shape.splineParams.railing.bPosts);
                    w.Key("railingPostInterval", shape.splineParams.railing.postInterval);
                    w.Key("railingPostBottom", shape.splineParams.railing.postBottom);
                    w.Key("railingPostTop", shape.splineParams.railing.postTop);
                    w.Key("railingPostSize", glm::vec2(shape.splineParams.railing.postSize.x, shape.splineParams.railing.postSize.y));
                    w.Key("railingPostLateral", shape.splineParams.railing.postLateral);
                    w.Key("railingLateralOffset", shape.splineParams.railing.lateralOffset);
                    if (!shape.splineParams.railing.lanes.IsEmpty()) {
                        w.Count("railingLanes", static_cast<uint32_t>(shape.splineParams.railing.lanes.Size()));
                        for (int li = 0; li < static_cast<int>(shape.splineParams.railing.lanes.Size()); li++) {
                            w.BeginBlock("l");
                            w.Key("lane", glm::vec2(shape.splineParams.railing.lanes[li].x, shape.splineParams.railing.lanes[li].y));
                            w.EndBlock();
                        }
                    }
                    w.EndBlock();
                }
                if (shape.text3DSource.IsValid()) {
                    w.BeginBlock("text3DSource");
                    w.Key("fontId", shape.text3DSource.fontId.id);
                    w.KeyStr("text", shape.text3DSource.text.View());
                    w.Key("depth", shape.text3DSource.depth);
                    w.Key("flatness", shape.text3DSource.flatness);
                    w.Key("tracking", shape.text3DSource.tracking);
                    w.Key("scale", shape.text3DSource.scale);
                    w.Key("wrapWidth", shape.text3DSource.wrapWidth);
                    w.Key("bendRadius", shape.text3DSource.bendRadius);
                    w.Key("smoothNormals", shape.text3DSource.bSmoothNormals);
                    w.Key("align", static_cast<uint32_t>(shape.text3DSource.align));
                    w.Key("anchor", static_cast<uint32_t>(shape.text3DSource.anchor));
                    w.Key("precise", shape.text3DSource.bPrecise);
                    w.EndBlock();
                }
                Component::SerializeProceduralShape(shape.proceduralParams, w);
                break;
        }

        w.EndBlock();
    }
}

void Component::PhysicsBodyDesc::Deserialize(PhysicsBodyDesc& comp, const Engine::TextReader& r)
{
    comp.motionType = static_cast<PhysicsMotionType>(r.UInt("motionType", 0));
    comp.mass = r.Float("mass", comp.mass);
    comp.friction = r.Float("friction", 0.0f);
    comp.restitution = r.Float("restitution", 0.0f);
    comp.motionQuality = static_cast<JPH::EMotionQuality>(r.UInt("motionQuality", 0));
    comp.layerOverride = static_cast<JPH::ObjectLayer>(r.UInt("layerOverride", 0xFFFF));
    comp.bEnhancedInternalEdgeRemoval = r.Bool("enhancedInternalEdgeRemoval", false);
    comp.bIsSensor = r.Bool("isSensor", false);
    comp.shapes.Clear();

    r.ForEachRecord("shapes", [&](const Engine::TextReader& s) {
        if (comp.shapes.IsFull()) { return; }
        PhysicsShapeDesc shape{};
        // Legacy migration: ConvexHull(3)/TriangleMesh(4)/Compound(5) collapsed into Collider(3)
        const uint32_t rawType = s.UInt("type", 0);
        shape.type = rawType >= static_cast<uint32_t>(PhysicsShapeType::Collider) ? PhysicsShapeType::Collider : static_cast<PhysicsShapeType>(rawType);

        shape.offset = s.Vec3("offset", shape.offset);
        shape.rotation = s.Quat("rotation", shape.rotation);
        shape.bakedScale = s.Vec3("bakedScale", shape.bakedScale);

        switch (shape.type) {
            case PhysicsShapeType::Box:
            {
                shape.box.halfExtents = s.Vec3("halfExtents", glm::vec3(0.5f));
                break;
            }
            case PhysicsShapeType::Sphere:
            {
                shape.sphere.radius = s.Float("radius", 0.5f);
                break;
            }
            case PhysicsShapeType::Capsule:
            {
                shape.capsule.radius = s.Float("radius", 0.5f);
                shape.capsule.halfHeight = s.Float("halfHeight", 0.5f);
                break;
            }
            case PhysicsShapeType::Collider:
            {
                shape.meshSourceModelId = Engine::ModelID(s.U64("meshSourceModelId", 0));
                shape.bMeshPrecise = s.Bool("meshPrecise", false);
                shape.proceduralParams = Component::DeserializeProceduralShape(s.Int("proceduralType", 0), s);
                const Engine::TextReader sp = s.Block("splineParams");
                if (sp.IsValid()) {
                    Engine::SplineParams spline{};
                    const Engine::TextReader sr = sp.Block("spline");
                    if (sr.IsValid()) { Engine::Spline::Deserialize(spline.spline, sr); }
                    spline.radius = sp.Float("radius", 0.5f);
                    spline.rollAngle = sp.Float("rollAngle", 0.0f);
                    spline.sides = sp.Int("sides", 8);
                    spline.segmentsPerSpan = sp.Int("segmentsPerSpan", 8);
                    spline.bCaps = sp.Bool("bCaps", true);
                    spline.bCrossPlanks = sp.Bool("bCrossPlanks", false);
                    spline.crossPlankInterval = sp.Int("crossPlankInterval", 4);
                    spline.crossPlankHeight = sp.Float("crossPlankHeight", 0.0f);
                    spline.crossPlankThickness = sp.Float("crossPlankThickness", 0.1f);
                    spline.crossPlankLength = sp.Float("crossPlankLength", 0.3f);
                    spline.profile.type = static_cast<Engine::SplineProfileType>(sp.Int("profileType", 0));
                    spline.profile.width = sp.Float("profileWidth", 0.4f);
                    spline.profile.height = sp.Float("profileHeight", 0.4f);
                    spline.profile.cornerRadius = sp.Float("profileCornerRadius", 0.08f);
                    spline.profile.cornerSegments = sp.Int("profileCornerSegments", 3);
                    spline.profile.thickness = sp.Float("profileThickness", 0.05f);
                    spline.railing.bEnabled = sp.Bool("railingEnabled", false);
                    spline.railing.bPosts = sp.Bool("railingPosts", true);
                    spline.railing.postInterval = sp.Int("railingPostInterval", 4);
                    spline.railing.postBottom = sp.Float("railingPostBottom", 0.0f);
                    spline.railing.postTop = sp.Float("railingPostTop", 1.0f);
                    const glm::vec2 postSize = sp.Vec2("railingPostSize", glm::vec2(0.05f, 0.05f));
                    spline.railing.postSize.x = postSize.x;
                    spline.railing.postSize.y = postSize.y;
                    spline.railing.postLateral = sp.Float("railingPostLateral", 0.0f);
                    spline.railing.lateralOffset = sp.Float("railingLateralOffset", 0.0f);
                    spline.railing.lanes.Clear();
                    sp.ForEachRecord("railingLanes", [&](const Engine::TextReader& l) {
                        if (spline.railing.lanes.Size() >= 8) { return; }
                        const glm::vec2 lane = l.Vec2("lane");
                        spline.railing.lanes.PushBack(Vec2{lane.x, lane.y});
                    });
                    shape.splineParams = spline;
                }
                const Engine::TextReader t3 = s.Block("text3DSource");
                if (t3.IsValid()) {
                    Text3DShapeSource src{};
                    src.fontId = Engine::FontID(t3.U64("fontId", 0));
                    t3.Str("text", src.text);
                    src.depth = t3.Float("depth", 0.2f);
                    src.flatness = t3.Float("flatness", 0.005f);
                    src.tracking = t3.Float("tracking", 0.0f);
                    src.scale = t3.Float("scale", 1.0f);
                    src.wrapWidth = t3.Float("wrapWidth", 0.0f);
                    src.bendRadius = t3.Float("bendRadius", 0.0f);
                    src.bSmoothNormals = t3.Bool("smoothNormals", true);
                    src.align = static_cast<Engine::Text3DAlign>(t3.UInt("align", static_cast<uint32_t>(Engine::Text3DAlign::Left)));
                    src.anchor = static_cast<Engine::Text3DAnchor>(t3.UInt("anchor", static_cast<uint32_t>(Engine::Text3DAnchor::Baseline)));
                    src.bPrecise = t3.Bool("precise", false);
                    shape.text3DSource = src;
                }
                break;
            }
        }

        comp.shapes.PushBack(shape);
    });
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

    bool modified = false;
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
                        modified = true;
                    }
                }
                if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Concave procedural shapes (Klein Bottle, Trefoil Knot, Bowl, Curved Ramp) can only be Static or Kinematic.");
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }

        modified |= ImGui::DragFloat("Mass", &component.mass, 0.1f, 0.001f, 10000.0f);
        modified |= ImGui::DragFloat("Friction", &component.friction, 0.01f, 0.0f, 10.0f);
        modified |= ImGui::DragFloat("Restitution", &component.restitution, 0.01f, 0.0f, 1.0f);

        const char* qualityTypes[] = {"Discrete", "LinearCast"};
        int currentQuality = static_cast<int>(component.motionQuality);
        if (ImGui::Combo("Motion Quality", &currentQuality, qualityTypes, IM_ARRAYSIZE(qualityTypes))) {
            component.motionQuality = static_cast<JPH::EMotionQuality>(currentQuality);
            modified = true;
        }

        int layer = static_cast<int>(component.layerOverride);
        if (layer == 0xFFFF) layer = -1;
        if (ImGui::InputInt("Layer Override", &layer)) {
            component.layerOverride = layer < 0 ? JPH::ObjectLayer(0xFFFF) : JPH::ObjectLayer(layer);
            modified = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("-1 = auto (derived from motion type)"); }

        modified |= ImGui::Checkbox("Enhanced Internal Edge Removal", &component.bEnhancedInternalEdgeRemoval);
        modified |= ImGui::Checkbox("Is Sensor", &component.bIsSensor);

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
                        modified = true;
                    }
                    if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("This concave procedural source only has a triangle-mesh collider; switch the body to Static or Kinematic to use it.");
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            modified |= ImGui::DragFloat3("Offset", &shape.offset.x, 0.01f);
            modified |= ImGui::DragFloat3("Baked Scale", &shape.bakedScale.x, 0.01f, 0.001f, 100.0f);

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
                    static constexpr Core::Array<const char*, 28> kProceduralNames = {
                        nullptr, "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch",
                        "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere",
                        "Hemisphere", "Pipe", "Tetrahedron", "Octahedron", "Icosahedron",
                        "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl", "Spiral Staircase", "Ring",
                        "Wall", "Lattice", "Corrugated Panel",
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
                modified = true;
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
                    modified = true;
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
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityRight * shape.sphere.radius, planeNormal,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.sphere.radius = glm::max(0.001f, glm::length(newPt - shapeCenter)); },
                                                     colorX);
                    break;
                }
                case PhysicsShapeType::Capsule:
                {
                    const Vec3 upPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityUp) * entityUp);
                    const Vec3 rightPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityUp * shape.capsule.halfHeight, upPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.capsule.halfHeight = glm::max(0.001f, glm::dot(newPt - shapeCenter, entityUp)); },
                                                     colorY);
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 1, shapeCenter + entityRight * shape.capsule.radius, rightPlane,
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
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityRight * shape.box.halfExtents.x, xPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.box.halfExtents.x = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityRight))); },
                                                     colorX);
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 1, shapeCenter + entityUp * shape.box.halfExtents.y, yPlane,
                                                     vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                     [&](Vec3 newPt) { shape.box.halfExtents.y = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityUp))); },
                                                     colorY);
                    bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 2, shapeCenter + entityForward * shape.box.halfExtents.z, zPlane,
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
                ImGuizmo::PushID(Editor::GizmoId::PHYSICS_SHAPE_OFFSET);
                Mat4 mat = glm::translate(Mat4(1.0f), shapeCenter);
                if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), ImGuizmo::TRANSLATE, ImGuizmo::WORLD, glm::value_ptr(mat))) {
                    shape.offset = Vec3(entityMatInv * Vec4(Vec3(mat[3]), 1.0f));
                    modified = true;
                }
                if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
                ImGuizmo::PopID();
                ImGuizmo::SetGizmoSizeClipSpace(0.1f);
            }

            // Rebuild the shape/body once on drag release rather than every frame of the drag.
            const bool bGizmoDragging = state->editor.activeDotHandleId != -1 || ImGuizmo::IsUsing();
            if (bGizmoWasDragging && !bGizmoDragging) {
                registry.patch<PhysicsBodyDesc>(entity);
                modified = true;
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
                modified = true;
            }
        }

        if (ImGui::Button("Add Collider")) {
            PhysicsShapeDesc desc{};
            desc.type = PhysicsShapeType::Box;
            desc.box.halfExtents = glm::vec3(0.5f);
            component.shapes.PushBack(desc);
            registry.patch<PhysicsBodyDesc>(entity);
            modified = true;
        }
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}
}
