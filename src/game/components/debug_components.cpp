//
// Created by William on 2026-01-30.
//

#include "debug_components.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"

void Game::Component::MotionBlurMovementComponent::Serialize(const MotionBlurMovementComponent& comp, Engine::TextWriter& w)
{
    w.KeyOpt("bIsHorizontal", comp.bIsHorizontal, false);
}

void Game::Component::MotionBlurMovementComponent::Deserialize(MotionBlurMovementComponent& comp, const Engine::TextReader& r)
{
    comp.bIsHorizontal = r.Bool("bIsHorizontal", comp.bIsHorizontal);
}
