//
// Created by William on 2026-01-30.
//

#include "camera_components.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

void Game::Component::FreeCameraComponent::Serialize(const FreeCameraComponent& comp, Engine::TextWriter& w)
{
    static const FreeCameraComponent DEF{};
    w.KeyOpt("moveSpeed", comp.moveSpeed, DEF.moveSpeed);
    w.KeyOpt("lookSpeed", comp.lookSpeed, DEF.lookSpeed);
}

void Game::Component::FreeCameraComponent::Deserialize(FreeCameraComponent& comp, const Engine::TextReader& r)
{
    comp.moveSpeed = r.Float("moveSpeed", comp.moveSpeed);
    comp.lookSpeed = r.Float("lookSpeed", comp.lookSpeed);
}
