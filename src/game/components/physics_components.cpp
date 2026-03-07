//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include <glm/gtc/type_ptr.hpp>

#include "component_serialization.h"
#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/systems/physics_system.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "physics/physics_system.h"

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
        }

        json["shapes"].push_back(shapeJson);
    }
}

template<>
void DeserializeComponent<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& comp, const nlohmann::json& json)
{
    comp.motionType = static_cast<Component::PhysicsMotionType>(json["motionType"].get<uint8_t>());
    comp.mass = json["mass"].get<float>();

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
        }

        comp.shapes.push_back(shape);
    }
}

template<>
ComponentEditorResult DrawComponentEditor<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    static int editShapeIdx = -1;
    static entt::entity editEntity = entt::null;
    static bool wantApply = false;

    Engine::GameState* state = registry.ctx().get<Engine::GameState*>();

    if (editEntity != entity) {
        editShapeIdx = -1;
        editEntity = entity;
        wantApply = false;
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
        if (ImGui::Combo("Motion Type", &currentMotion, motionTypes, IM_ARRAYSIZE(motionTypes)))
            component.motionType = static_cast<Component::PhysicsMotionType>(currentMotion);

        ImGui::DragFloat("Mass", &component.mass, 0.1f, 0.001f, 10000.0f);

        const char* qualityTypes[] = {"Discrete", "LinearCast"};
        int currentQuality = static_cast<int>(component.motionQuality);
        if (ImGui::Combo("Motion Quality", &currentQuality, qualityTypes, IM_ARRAYSIZE(qualityTypes)))
            component.motionQuality = static_cast<JPH::EMotionQuality>(currentQuality);

        const glm::mat4 view = viewFamily.mainView.currentViewData.view;
        const glm::mat4 proj = viewFamily.mainView.currentViewData.proj;
        auto* transform = registry.try_get<Component::TransformComponent>(entity);

        ImGui::SeparatorText("Shapes");
        int shapeToRemove = -1;
        for (int i = 0; i < static_cast<int>(component.shapes.size()); ++i) {
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
                if (isEditing) {
                    editShapeIdx = -1;
                    wantApply = true;
                }
                else {
                    editShapeIdx = i;
                }
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
                const char* shapeTypes[] = {"Box", "Sphere", "Capsule"};
                int currentShape = static_cast<int>(shape.type);
                if (ImGui::Combo("Shape", &currentShape, shapeTypes, IM_ARRAYSIZE(shapeTypes)))
                    shape.type = static_cast<Component::PhysicsShapeType>(currentShape);

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
                }
                ImGui::TreePop();
            }

            if (isEditing && transform && hasGizmoClaim) {
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

                // Offset
                applyStyle({1.0f, 0.55f, 0.05f, 1.0f}, {1.0f, 0.85f, 0.35f, 1.0f}, 0.10f);
                gizmo(shapeCenter, [&](glm::vec3 newCenter) {
                    shape.offset = glm::vec3(entityMatInv * glm::vec4(newCenter, 1.0f));
                });

                // Control Points
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
                }

                ImGuizmo::GetStyle() = savedStyle;
                ImGuizmo::SetGizmoSizeClipSpace(0.1f);

                // Debug shape preview
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
            }

            ImGui::PopID();
        }

        if (shapeToRemove >= 0) {
            component.shapes.erase(component.shapes.begin() + shapeToRemove);
        }

        if (ImGui::Button("Add Shape")) {
            Component::PhysicsShapeDesc desc{};
            desc.type = Component::PhysicsShapeType::Box;
            desc.box.halfExtents = glm::vec3(0.5f);
            component.shapes.push_back(desc);
        }

        ImGui::SeparatorText("Actions");
        if (wantApply || ImGui::Button("Apply (Recreate Body)")) {
            wantApply = false;
            auto* ctx = registry.ctx().get<Core::EngineContext*>();
            JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

            if (auto* body = registry.try_get<Component::PhysicsBodyComponent>(entity)) {
                bodyInterface.RemoveBody(body->bodyID);
                bodyInterface.DestroyBody(body->bodyID);
                registry.remove<Component::PhysicsBodyComponent>(entity);
                registry.remove<Component::DynamicPhysicsBodyComponent>(entity);
            }

            OnComponentAdded<Component::PhysicsBodyDesc>(component, registry, entity);
        }
    }

    return {.requestRemoval = remove};
}

template<>
void OnPlayStart<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    if (auto* transform = registry.try_get<Component::TransformComponent>(entity)) {
        JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();
        JPH::RVec3 startPos(transform->translation.x, transform->translation.y, transform->translation.z);
        JPH::Quat startRot(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
        auto bodyId = CreateBodyFromDesc(bodyInterface, component, startPos, startRot);
        if (!bodyId.IsInvalid()) {
            registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
            if (component.motionType == Component::PhysicsMotionType::Dynamic) {
                registry.emplace<Component::DynamicPhysicsBodyComponent>(entity, transform->translation, transform->rotation);
            }
        }
    }
    else {
        LOG_WARN(Game, "PhysicsBodyDesc on entity without TransformComponent, skipping");
    }
}

template<>
void OnComponentAdded<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{}

template<>
void OnPlayStop<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
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
void OnComponentRemoved<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<Component::PhysicsBodyDesc>(entity);
}

template<>
ComponentEditorResult DrawComponentEditor<Component::DrawPhysicsDebugTag>(Component::DrawPhysicsDebugTag& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    ImGui::CollapsingHeader("Physics Debug Draw", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletephysicscomponent");
    ImGui::PopStyleColor();

    return {.requestRemoval = remove};
}
}
