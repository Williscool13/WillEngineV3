//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include "engine/engine_api.h"

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

void PhysicsBodyDesc::Serialize(const PhysicsBodyDesc& comp, nlohmann::json& json)
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
            case PhysicsShapeType::Box:
                shapeJson["halfExtents"] = {shape.box.halfExtents.x, shape.box.halfExtents.y, shape.box.halfExtents.z};
                break;
            case PhysicsShapeType::Sphere:
                shapeJson["radius"] = shape.sphere.radius;
                break;
            case PhysicsShapeType::Capsule:
                shapeJson["radius"] = shape.capsule.radius;
                shapeJson["halfHeight"] = shape.capsule.halfHeight;
                break;
        }

        json["shapes"].push_back(shapeJson);
    }
}

void PhysicsBodyDesc::Deserialize(PhysicsBodyDesc& comp, const nlohmann::json& json)
{
    comp.motionType = static_cast<PhysicsMotionType>(json["motionType"].get<uint8_t>());
    comp.mass = json["mass"].get<float>();

    for (const auto& shapeJson : json["shapes"]) {
        PhysicsShapeDesc shape{};
        shape.type = static_cast<PhysicsShapeType>(shapeJson["type"].get<uint8_t>());

        auto& o = shapeJson["offset"];
        shape.offset = {o[0].get<float>(), o[1].get<float>(), o[2].get<float>()};

        auto& r = shapeJson["rotation"];
        shape.rotation = {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};

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
        }

        comp.shapes.push_back(shape);
    }
}
}
