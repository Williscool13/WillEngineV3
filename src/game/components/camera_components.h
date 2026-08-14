//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_CAMERACOMPONENT_H
#define WILL_ENGINE_CAMERACOMPONENT_H

#include "render/interface/render_interface.h"

namespace Engine
{
class TextWriter;
class TextReader;
}

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
    static constexpr const char* COMPONENT_NAME = "FreeCameraComponent";

    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;

    static void Serialize(const FreeCameraComponent& comp, Engine::TextWriter& w);
    static void Deserialize(FreeCameraComponent& comp, const Engine::TextReader& r);
};
}


#endif //WILL_ENGINE_CAMERACOMPONENT_H
