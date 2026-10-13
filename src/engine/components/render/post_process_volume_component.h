//
// Created by William on 2026-10-12.
//

#ifndef WILL_ENGINE_POST_PROCESS_VOLUME_COMPONENT_H
#define WILL_ENGINE_POST_PROCESS_VOLUME_COMPONENT_H

#include <string_view>

#include <entt/entt.hpp>

#include "core/containers/inline_string.h"
#include "core/containers/inline_vector.h"
#include "core/containers/span.h"
#include "engine/engine_api.h"
#include "engine/reflection/reflection.h"

namespace Core
{
struct ViewFamily;
}

namespace Engine::Component
{
struct PostProcessOverride
{
    Core::InlineString<32> field;
    Vec3 value{0.0f};

    WILL_REFLECT(PostProcessOverride,
        WILL_FIELD(field),
        WILL_FIELD(value))
};

struct PostProcessVolumeComponent
{
    static constexpr const char* COMPONENT_NAME = "PostProcessVolumeComponent";
    static constexpr uint32_t MAX_OVERRIDES = 16;

    bool bEnabled{true};
    bool bUnbound{false};
    int32_t priority{0};
    Vec3 extents{5.0f, 5.0f, 5.0f};
    float blendRadius{1.0f};
    float blendWeight{1.0f};
    Core::InlineVector<PostProcessOverride, MAX_OVERRIDES> overrides;

    WILL_REFLECT(PostProcessVolumeComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(bUnbound),
        WILL_FIELD(priority),
        WILL_FIELD(extents, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(blendRadius, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(blendWeight, .min = 0.0f, .max = 1.0f, .speed = 0.01f),
        WILL_FIELD(overrides))

    static void Sanitize(PostProcessVolumeComponent& comp);

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);

    /** 0 outside the blend band, blendWeight inside the box. */
    static float CameraWeight(const PostProcessVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, const Vec3& cameraPos);
};

struct PostProcessOverrideField
{
    const char* name;
    const char* label;
    const char* group;
    float min;
    float max;
    float speed;
    bool bColor;
    float* (*resolve)(Core::ViewFamily& viewFamily);
};

Core::Span<const PostProcessOverrideField> PostProcessOverrideFields();

const PostProcessOverrideField* FindPostProcessOverrideField(std::string_view name);
}

#endif //WILL_ENGINE_POST_PROCESS_VOLUME_COMPONENT_H
