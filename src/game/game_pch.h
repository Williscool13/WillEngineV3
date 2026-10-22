//
// Created by William on 2026-10-19.
//

#ifndef WILL_ENGINE_GAME_PCH_H
#define WILL_ENGINE_GAME_PCH_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <spdlog/spdlog.h>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/components/component_registration.h"
#include "engine/components/core_components.h"
#include "engine/components/common_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_components.h"
#include "engine/systems/scene_system.h"
#include "physics/physics_world.h"

#endif //WILL_ENGINE_GAME_PCH_H
