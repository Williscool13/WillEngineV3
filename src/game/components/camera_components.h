//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include "core/include/render_interface.h"
#include <json/nlohmann/json_fwd.hpp>

namespace Game::Component
{
struct CameraComponent
{
    Core::ViewData currentViewData;
    Core::ViewData previousViewData;
};

struct MainViewportComponent
{};

struct FreeCameraComponent
{
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;
    float yaw = 0.0f;
    float pitch = 0.0f;

    static void Serialize(const FreeCameraComponent& comp, nlohmann::json& json);
    static void Deserialize(FreeCameraComponent& comp, const nlohmann::json& json);
};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
