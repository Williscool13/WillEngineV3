//
// Created by William on 2025-12-24.
//

#ifndef WILL_ENGINE_COMPONENT_INCLUDE_H
#define WILL_ENGINE_COMPONENT_INCLUDE_H

namespace Game
{
// If the crash on hotreload happens again, look into using this (previously had to do this manually in game load)
// SPDLOG_TRACE("[Game] Registering engine component types:");
// SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::TransformComponent>().hash());
// SPDLOG_TRACE("  CameraComponent: {}", entt::type_id<Game::CameraComponent>().hash());
// SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Game::MainViewportComponent>().hash());
// SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FreeCameraComponent>().hash());
#define REGISTER_COMPONENT() \
    static inline const auto _entt_type_registration = entt::type_id<std::remove_cvref_t<decltype(*this)>>().hash()
}

#endif //WILL_ENGINE_COMPONENT_INCLUDE_H