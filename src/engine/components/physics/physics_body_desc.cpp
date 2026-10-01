//
// Created by William on 2026-03-21.
//

#include "physics_body_desc.h"

#include <glm/gtc/type_ptr.hpp>

#include "physics_body_component.h"
#include "physics_components.h"
#include "physics_shape_helpers.h"
#include "engine/components/component_editor.h"
#include "engine/editor/editor_gizmo_helpers.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/resources/physics/collider_generation.h"
#include "engine/engine_api.h"
#include "engine/reflection/reflection_serialize.h"
#include "engine/components/core_components.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/text3d_component.h"

namespace Engine::Component
{
static void ApplyRenderTransform(PhysicsShapeDesc& shape, const glm::vec3& scale, const glm::vec3& renderOffset, const glm::quat& renderRotation)
{
    shape.offset = scale * renderOffset + renderRotation * shape.offset;
    shape.rotation = renderRotation * shape.rotation;
}

// A concave collider source: a non-analytic procedural (Klein Bottle, Trefoil Knot, Bowl, Curved Ramp) or a precise Text3D. Only a triangle mesh, so it cannot back a dynamic body.
static bool ShapeIsConcaveExotic(const PhysicsShapeDesc& shape)
{
    const auto* collider = std::get_if<ColliderShape>(&shape.geometry);
    if (!collider) { return false; }
    if (!std::holds_alternative<std::monostate>(collider->proceduralParams)) {
        return !Engine::CanBuildProceduralCollider(collider->proceduralParams);
    }
    if (collider->meshSourceModelId.IsValid()) {
        return collider->bMeshPrecise;
    }
    return collider->text3DSource.IsValid() && collider->text3DSource.bPrecise;
}

static void ReleaseColliders(entt::registry& registry, entt::entity entity)
{
    auto* runtime = registry.try_get<PhysicsShapeRuntime>(entity);
    if (!runtime) { return; }
    auto* state = registry.ctx().get<Engine::EngineState*>();
    for (const Engine::PhysicsColliderHandle handle : runtime->colliders) {
        if (handle.IsValid()) {
            state->commandQueue.Push({.type = CommandType::ColliderRelease, .payload = {.colliderHandle = handle}});
        }
    }
    runtime->colliders.Clear();
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
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::PhysicsBodyConstruct, .entity = entity});
}

void PhysicsBodyDesc::DeferredConstruct(entt::registry& registry, entt::entity entity)
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
            const auto* meta = ctx->assetManager->GetModelMetadata(sm->modelId);
            if (meta && meta->bounds.aabb.min.x <= meta->bounds.aabb.max.x) {
                box.geometry = BoxShape{meta->bounds.aabb.HalfExtents() * scale};
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
            ColliderShape collider{};
            FillSplineParams(collider.splineParams, *splm);
            PhysicsShapeDesc s{};
            s.geometry = collider;
            s.bakedScale = scale;
            component.shapes.PushBack(s);
        }
        else {
            component.shapes.PushBack({});
        }
    }

    auto& runtime = registry.get_or_emplace<PhysicsShapeRuntime>(entity);
    runtime.colliders.Clear();
    for (size_t i = 0; i < component.shapes.Size(); ++i) {
        runtime.colliders.PushBack({});
    }
    runtime.shapeRef = nullptr;

    // Source loaded (freeze-gated) in PhysicsMeshPendingKickoff so a model hot-reload can release this body's ref and re-acquire after the drain.
    bool bHasMeshShape = false;
    for (const auto& shape : component.shapes) {
        if (std::holds_alternative<ColliderShape>(shape.geometry)) {
            bHasMeshShape = true;
            break;
        }
    }
    if (bHasMeshShape) {
        registry.remove<PhysicsMeshLoadingTag>(entity);
        registry.emplace_or_replace<PendingPhysicsMeshTag>(entity);
    }

    registry.emplace_or_replace<PendingPhysicsShapeCreationTag>(entity);
    registry.emplace_or_replace<PendingPhysicsBodyCreationTag>(entity);
}

void PhysicsBodyDesc::RequestRebuild(entt::registry& registry, entt::entity entity)
{
    ReleaseColliders(registry, entity);
    registry.remove<PendingPhysicsMeshTag, PhysicsMeshLoadingTag, PendingPhysicsShapeCreationTag, PendingPhysicsBodyCreationTag>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();
    state->commandQueue.Push({.type = CommandType::PhysicsBodyConstruct, .entity = entity});
}

void PhysicsBodyDesc::OnDestroy(entt::registry& registry, entt::entity entity)
{
    ReleaseColliders(registry, entity);
    registry.remove<PhysicsShapeRuntime>(entity);
    auto* state = registry.ctx().get<EngineState*>();
    state->commandQueue.Push({.type = CommandType::PhysicsBodyRemove, .entity = entity});
}
}


namespace Engine
{
void Component::PhysicsBodyDesc::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    RequestRebuild(registry, entity);
}

/** Multi-edit replaces a whole shape list with the primary's, so shapes are edited on single selections only. */
Engine::ComponentEditorResult Component::PhysicsBodyDesc::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const PhysicsBodyDesc before = edit.Get<PhysicsBodyDesc>();
    PhysicsBodyDesc component = before;
    bool bCommit = false;
    static int editShapeIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool bGizmoWasDragging = false;

    auto state = edit.State();
    auto ctx = registry.ctx().get<Engine::EngineContext*>();

    if (editEntity != entity || edit.IsMulti()) {
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
                        bCommit = true;
                    }
                }
                if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Concave procedural shapes (Klein Bottle, Trefoil Knot, Bowl, Curved Ramp) can only be Static or Kinematic.");
                }
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat("Mass", &component.mass, 0.1f, 0.001f, 10000.0f, EditWidgets::MixedFormat(edit.IsMixed(&PhysicsBodyDesc::mass), "%.3f"));
        bCommit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Friction", &component.friction, 0.01f, 0.0f, 10.0f, EditWidgets::MixedFormat(edit.IsMixed(&PhysicsBodyDesc::friction), "%.3f"));
        bCommit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Restitution", &component.restitution, 0.01f, 0.0f, 1.0f, EditWidgets::MixedFormat(edit.IsMixed(&PhysicsBodyDesc::restitution), "%.3f"));
        bCommit |= ImGui::IsItemDeactivatedAfterEdit();

        const char* qualityTypes[] = {"Discrete", "LinearCast"};
        int currentQuality = static_cast<int>(component.motionQuality);
        if (ImGui::Combo("Motion Quality", &currentQuality, qualityTypes, IM_ARRAYSIZE(qualityTypes))) {
            component.motionQuality = static_cast<JPH::EMotionQuality>(currentQuality);
            bCommit = true;
        }

        int layer = static_cast<int>(component.layerOverride);
        if (layer == 0xFFFF) layer = -1;
        if (ImGui::InputInt("Layer Override", &layer)) {
            component.layerOverride = layer < 0 ? JPH::ObjectLayer(0xFFFF) : JPH::ObjectLayer(layer);
            bCommit = true;
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("-1 = auto (derived from motion type)"); }

        bCommit |= ImGui::Checkbox("Enhanced Internal Edge Removal", &component.bEnhancedInternalEdgeRemoval);
        bCommit |= ImGui::Checkbox("Is Sensor", &component.bIsSensor);

        ImGui::BeginDisabled(edit.IsMulti());
        if (edit.IsMulti()) { ImGui::TextDisabled("Shapes are edited one entity at a time"); }

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
            static constexpr size_t COLLIDER_INDEX = 3;
            const size_t currentType = shape.geometry.index();
            if (ImGui::BeginCombo("Shape Type", kShapeTypes[currentType])) {
                for (size_t s = 0; s < std::size(kShapeTypes); ++s) {
                    // Exotics are concave and cannot be dynamic
                    const bool bDisabled = bIsDynamic && s == COLLIDER_INDEX && bRenderSourceExotic;
                    ImGui::BeginDisabled(bDisabled);
                    if (ImGui::Selectable(kShapeTypes[s], currentType == s) && currentType != s) {
                        const bool wasMesh = currentType == COLLIDER_INDEX;
                        Engine::EmplaceVariantIndex(shape.geometry, s);
                        if (s == COLLIDER_INDEX) {
                            FitMeshShapeToEntity(registry, entity, shape, scale);
                        }
                        else if (wasMesh) {
                            if (bModelLoaded) {
                                FitPrimitiveShapeToEntity(registry, entity, shape, scale, fitModel->bounds);
                            }
                            else {
                                shape.offset = glm::vec3(0.0f);
                                shape.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                                shape.bakedScale = glm::vec3(1.0f);
                            }
                        }
                        bCommit = true;
                    }
                    if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("This concave procedural source only has a triangle-mesh collider; switch the body to Static or Kinematic to use it.");
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat3("Offset", &shape.offset.x, 0.01f);
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat3("Baked Scale", &shape.bakedScale.x, 0.01f, 0.001f, 100.0f);
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();

            bool bAnyChange = false;
            if (auto* box = std::get_if<BoxShape>(&shape.geometry)) {
                ImGui::DragFloat3("Half Extents", &box->halfExtents.x, 0.01f, 0.001f, 100.0f);
                bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            }
            else if (auto* sphere = std::get_if<SphereShape>(&shape.geometry)) {
                ImGui::DragFloat("Radius", &sphere->radius, 0.01f, 0.001f, 100.0f);
                bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            }
            else if (auto* capsule = std::get_if<CapsuleShape>(&shape.geometry)) {
                ImGui::DragFloat("Radius", &capsule->radius, 0.01f, 0.001f, 100.0f);
                bCommit |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat("Half Height", &capsule->halfHeight, 0.01f, 0.001f, 100.0f);
                bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            }
            else if (auto* collider = std::get_if<ColliderShape>(&shape.geometry)) {
                bool bHasAny = false;
                const auto* meta = ctx->assetManager->GetModelMetadata(collider->meshSourceModelId);
                static constexpr Core::Array<const char*, 31> kProceduralNames = {
                    nullptr, "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch",
                    "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere",
                    "Hemisphere", "Pipe", "Tetrahedron", "Octahedron", "Icosahedron",
                    "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl", "Spiral Staircase", "Ring",
                    "Wall", "Lattice", "Corrugated Panel", "Terrace", "Pyramid", "Slanted Beam",
                };
                const size_t idx = collider->proceduralParams.index();

                if (meta) {
                    ImGui::Text("Mesh Source: %s", meta->name.c_str());
                    bHasAny = true;

                    ImGui::BeginDisabled(bIsDynamic);
                    bAnyChange |= ImGui::Checkbox("Precise (Triangle Mesh)", &collider->bMeshPrecise);
                    if (bIsDynamic && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("A precise mesh collider is concave and cannot back a dynamic body.");
                    }
                    ImGui::EndDisabled();
                }
                else if (idx > 0 && idx < kProceduralNames.Size()) {
                    ImGui::Text("Mesh Source: Procedural %s", kProceduralNames[idx]);
                    bHasAny = true;
                    if (auto* terrace = std::get_if<Engine::TerraceParams>(&collider->proceduralParams)) {
                        bool bRamp = terrace->profile == Engine::TerraceProfile::Ramp;
                        if (ImGui::Checkbox("Ramp Collision", &bRamp)) {
                            terrace->profile = bRamp ? Engine::TerraceProfile::Ramp : Engine::TerraceProfile::Steps;
                            bAnyChange = true;
                        }
                        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Collide with the slope through the step nosings, whatever the mesh draws; Auto-Fit copies the mesh's profile back"); }
                    }
                }
                else if (!collider->splineParams.spline.points.IsEmpty()) {
                    ImGui::Text("Mesh Source: Procedural Spline");
                    bHasAny = true;
                }
                else if (collider->text3DSource.IsValid()) {
                    ImGui::Text("Mesh Source: 3D Text");
                    bHasAny = true;

                    ImGui::BeginDisabled(bIsDynamic);
                    bAnyChange |= ImGui::Checkbox("Precise (Triangle Mesh)", &collider->text3DSource.bPrecise);
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
                        collider->meshSourceModelId = Engine::ModelID::INVALID;
                        collider->proceduralParams = std::monostate{};
                        collider->splineParams.spline.points.Clear();
                        collider->text3DSource = {};
                        bAnyChange = true;
                    }
                }
            }
            bCommit |= bAnyChange;

            //
            {
                const bool isMeshType = std::holds_alternative<ColliderShape>(shape.geometry);

                ImGui::BeginDisabled(!bModelLoaded && !isMeshType);
                if (ImGui::Button("Auto-Fit")) {
                    if (isMeshType) {
                        FitMeshShapeToEntity(registry, entity, shape, scale);
                    }
                    else {
                        FitPrimitiveShapeToEntity(registry, entity, shape, scale, fitModel->bounds);
                    }
                    bCommit = true;
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
            if (auto* sphere = std::get_if<SphereShape>(&shape.geometry)) {
                const Vec3 planeNormal = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityRight * sphere->radius, planeNormal,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { sphere->radius = glm::max(0.001f, glm::length(newPt - shapeCenter)); },
                                                 colorX);
            }
            else if (auto* capsule = std::get_if<CapsuleShape>(&shape.geometry)) {
                const Vec3 upPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityUp) * entityUp);
                const Vec3 rightPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityUp * capsule->halfHeight, upPlane,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { capsule->halfHeight = glm::max(0.001f, glm::dot(newPt - shapeCenter, entityUp)); },
                                                 colorY);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 1, shapeCenter + entityRight * capsule->radius, rightPlane,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { capsule->radius = glm::max(0.001f, glm::length(newPt - shapeCenter)); },
                                                 colorX);
            }
            else if (auto* box = std::get_if<BoxShape>(&shape.geometry)) {
                const Vec3 xPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityRight) * entityRight);
                const Vec3 yPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityUp) * entityUp);
                const Vec3 zPlane = glm::normalize(vd.cameraForward - glm::dot(vd.cameraForward, entityForward) * entityForward);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 0, shapeCenter + entityRight * box->halfExtents.x, xPlane,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { box->halfExtents.x = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityRight))); },
                                                 colorX);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 1, shapeCenter + entityUp * box->halfExtents.y, yPlane,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { box->halfExtents.y = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityUp))); },
                                                 colorY);
                bHandleBusy |= Editor::DotHandle(Editor::DotHandleId::PHYSICS_SHAPE_BASE + 2, shapeCenter + entityForward * box->halfExtents.z, zPlane,
                                                 vd.view, vd.proj, viewport, vd.cameraPos, state,
                                                 [&](Vec3 newPt) { box->halfExtents.z = glm::max(0.001f, glm::abs(glm::dot(newPt - shapeCenter, entityForward))); },
                                                 colorZ);
            }

            if (!bHandleBusy && state->editor.activeDotHandleId == -1) {
                ImGuizmo::SetGizmoSizeClipSpace(0.10f);
                ImGuizmo::PushID(Editor::GizmoId::PHYSICS_SHAPE_OFFSET);
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
                bCommit = true;
            }
            bGizmoWasDragging = bGizmoDragging;

            constexpr Vec4 editColorX = Editor::DEBUG_AXIS_X;
            constexpr Vec4 editColorY = Editor::DEBUG_AXIS_Y;
            constexpr Vec4 editColorZ = Editor::DEBUG_AXIS_Z;
            if (const auto* sphere = std::get_if<SphereShape>(&shape.geometry)) {
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {shapeCenter, sphere->radius, editColorX});
            }
            else if (const auto* capsule = std::get_if<CapsuleShape>(&shape.geometry)) {
                const Vec3 top = shapeCenter + entityUp * capsule->halfHeight;
                const Vec3 bot = shapeCenter - entityUp * capsule->halfHeight;
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {top, capsule->radius, editColorY});
                DEBUG_ADD_SPHERE(viewFamily.debugSpheres, {bot, capsule->radius, editColorY});
                DEBUG_ADD_LINE(viewFamily.debugLines, {top + entityRight * capsule->radius, bot + entityRight * capsule->radius, editColorX});
                DEBUG_ADD_LINE(viewFamily.debugLines, {top - entityRight * capsule->radius, bot - entityRight * capsule->radius, editColorX});
                DEBUG_ADD_LINE(viewFamily.debugLines, {top + entityForward * capsule->radius, bot + entityForward * capsule->radius, editColorZ});
                DEBUG_ADD_LINE(viewFamily.debugLines, {top - entityForward * capsule->radius, bot - entityForward * capsule->radius, editColorZ});
            }
            else if (const auto* box = std::get_if<BoxShape>(&shape.geometry)) {
                const glm::vec3 he = box->halfExtents;
                const Quat boxRot = transform->rotation * shape.rotation;
                const Vec3 bx = boxRot * Vec3(he.x, 0.0f, 0.0f);
                const Vec3 by = boxRot * Vec3(0.0f, he.y, 0.0f);
                const Vec3 bz = boxRot * Vec3(0.0f, 0.0f, he.z);
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + bx, he.y, he.z, glm::normalize(by), glm::normalize(bz), editColorX, 0.02f});
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - bx, he.y, he.z, glm::normalize(by), glm::normalize(bz), editColorX, 0.02f});
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + by, he.x, he.z, glm::normalize(bx), glm::normalize(bz), editColorY, 0.02f});
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - by, he.x, he.z, glm::normalize(bx), glm::normalize(bz), editColorY, 0.02f});
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter + bz, he.x, he.y, glm::normalize(bx), glm::normalize(by), editColorZ, 0.02f});
                DEBUG_ADD_RECT(viewFamily.debugRects, {shapeCenter - bz, he.x, he.y, glm::normalize(bx), glm::normalize(by), editColorZ, 0.02f});
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
                bCommit = true;
            }
        }

        if (ImGui::Button("Add Collider")) {
            component.shapes.PushBack({});
            bCommit = true;
        }
        ImGui::EndDisabled();
    }

    edit.PreviewDiff(before, component);
    if (bCommit) {
        edit.Commit<PhysicsBodyDesc>();
    }

    return {.bRequestRemoval = remove};
}
}
