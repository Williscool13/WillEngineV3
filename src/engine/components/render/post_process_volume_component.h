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
struct ViewFamilyDebugDraws;
struct ViewFamilyFog;
struct ViewFamilySettings;
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
    float blendRadius{1.0f};
    float blendWeight{1.0f};
    Core::InlineVector<PostProcessOverride, MAX_OVERRIDES> overrides;

    WILL_REFLECT(PostProcessVolumeComponent,
        WILL_FIELD(bEnabled),
        WILL_FIELD(bUnbound),
        WILL_FIELD(priority),
        WILL_FIELD(blendRadius, .min = 0.0f, .speed = 0.05f),
        WILL_FIELD(blendWeight, .min = 0.0f, .max = 1.0f, .speed = 0.01f),
        WILL_FIELD(overrides))

    static void Sanitize(PostProcessVolumeComponent& comp);

    static Engine::ComponentEditorResult DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name);

    static float CameraWeight(const PostProcessVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, const Vec3& cameraPos);

    static void DrawBounds(Core::ViewFamilyDebugDraws& debugDraws, const PostProcessVolumeComponent& volume, const Vec3& center, const Quat& rotation, const Vec3& scale, float lineWidth, bool bSelected);
};

enum class PostProcessOverrideKind : uint8_t { Float, Color, Bool, Enum };

struct PostProcessOverrideField
{
    const char* name;
    const char* label;
    const char* group;
    PostProcessOverrideKind kind;
    float min;
    float max;
    float speed;
    void* (*resolve)(Core::ViewFamilySettings& settings, Core::ViewFamilyFog& fog);
    Core::Span<const char* const> options;
    int32_t optionOffset;
};

const PostProcessOverrideField* FindPostProcessOverrideField(std::string_view name);

Vec3 ReadPostProcessOverrideField(const PostProcessOverrideField& field, Core::ViewFamilySettings& settings, Core::ViewFamilyFog& fog);

void ApplyPostProcessOverride(const PostProcessOverrideField& field, Core::ViewFamilySettings& settings, Core::ViewFamilyFog& fog, const Vec3& value, float weight);
}

#endif //WILL_ENGINE_POST_PROCESS_VOLUME_COMPONENT_H
