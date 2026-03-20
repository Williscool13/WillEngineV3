//
// Created by William on 2026-03-20.
//

#ifndef WILL_ENGINE_CHARACTER_COMPONENTS_H
#define WILL_ENGINE_CHARACTER_COMPONENTS_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace Game::Component
{
struct CharacterPhysicsComponent
{
    JPH::Ref<JPH::CharacterVirtual> character;
    JPH::ShapeRefC capsuleShape;
    JPH::ShapeRefC standingShape;
    // JPH::ShapeRefC crouchingShape;
};
}

#endif //WILL_ENGINE_CHARACTER_COMPONENTS_H
