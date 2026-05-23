//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_JSON_HELPERS_H
#define WILL_ENGINE_JSON_HELPERS_H

#include <json/nlohmann/json.hpp>
#include "core/types/math.h"

namespace glm
{
inline void to_json(nlohmann::json& j, const vec2& v) { j = {v.x, v.y}; }
inline void to_json(nlohmann::json& j, const vec3& v) { j = {v.x, v.y, v.z}; }
inline void to_json(nlohmann::json& j, const vec4& v) { j = {v.x, v.y, v.z, v.w}; }

inline void from_json(const nlohmann::json& j, vec2& v) { v = {j[0].get<float>(), j[1].get<float>()}; }
inline void from_json(const nlohmann::json& j, vec3& v) { v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()}; }
inline void from_json(const nlohmann::json& j, vec4& v) { v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()}; }
} // glm

#endif //WILL_ENGINE_JSON_HELPERS_H
