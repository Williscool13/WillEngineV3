//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

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
ComponentEditorResult DrawComponentEditor<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity)
{
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

        ImGui::SeparatorText("Shapes");
        for (size_t i = 0; i < component.shapes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& shape = component.shapes[i];

            if (ImGui::TreeNode("", "Shape %zu", i)) {
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
            ImGui::PopID();
        }

        if (ImGui::Button("Add Shape")) {
            Component::PhysicsShapeDesc desc{};
            desc.type = Component::PhysicsShapeType::Box;
            desc.box.halfExtents = glm::vec3(0.5f);
            component.shapes.push_back(desc);
        }

        ImGui::SeparatorText("Actions");
        if (ImGui::Button("Apply (Recreate Body)")) {
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
void OnComponentRemoved<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
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

    registry.remove<Component::PhysicsBodyDesc>(entity);
}

template<>
void OnComponentAdded<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity)
{
    auto* ctx = registry.ctx().get<Core::EngineContext*>();
    if (auto* transform = registry.try_get<Component::TransformComponent>(entity)) {
        JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();
        JPH::RVec3 pos(transform->translation.x, transform->translation.y, transform->translation.z);
        JPH::Quat rot(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
        auto bodyId = CreateBodyFromDesc(bodyInterface, component, pos, rot);
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
ComponentEditorResult DrawComponentEditor<Component::DrawPhysicsDebugTag>(Component::DrawPhysicsDebugTag& component, const Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity)
{
    ImGui::CollapsingHeader("Physics Debug Draw", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletephysicscomponent");
    ImGui::PopStyleColor();

    return {.requestRemoval = remove};
}
}
