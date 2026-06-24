//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include <json/nlohmann/json_fwd.hpp>

#include "../../render/interface/render_interface.h"

namespace Game::Component
{
struct CameraComponent
{
    Core::ViewData currentViewData;
    Core::ViewData previousViewData;
};

struct GameCameraTag
{};

struct EditorCameraTag
{};

struct FreeCameraComponent
{
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;

    static void Serialize(const FreeCameraComponent& comp, nlohmann::json& json);
    static void Deserialize(FreeCameraComponent& comp, const nlohmann::json& json);
};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
