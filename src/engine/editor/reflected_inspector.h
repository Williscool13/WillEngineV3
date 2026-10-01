//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_REFLECTED_INSPECTOR_H
#define WILL_ENGINE_REFLECTED_INSPECTOR_H

#include <cinttypes>
#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "imgui.h"

#include "core/containers/inline_string.h"
#include "core/math/euler.h"
#include "engine/component_registry.h"
#include "engine/editor/edit_context.h"
#include "engine/editor/edit_widgets.h"
#include "engine/reflection/reflection.h"
#include "engine/reflection/reflection_serialize.h"

namespace Engine
{
template<Reflected C>
void DrawReflectedFields(EditContext& edit)
{
    ForEachField<C>([&edit](const auto& f) {
        using M = FieldMemberType<decltype(f)>;
        const float speed = f.attrs.speed > 0.0f ? f.attrs.speed : 0.01f;
        if constexpr (std::is_same_v<M, float>) {
            EditWidgets::DragFloat(edit, f.name, f.member, speed, f.attrs.min, f.attrs.max);
        }
        else if constexpr (std::is_same_v<M, bool>) {
            EditWidgets::Checkbox(edit, f.name, f.member);
        }
        else if constexpr (std::is_integral_v<M>) {
            EditWidgets::DragInt(edit, f.name, f.member, f.attrs.speed > 0.0f ? f.attrs.speed : 1.0f, static_cast<int32_t>(f.attrs.min), static_cast<int32_t>(f.attrs.max));
        }
        else if constexpr (std::is_same_v<M, glm::vec2>) {
            EditWidgets::DragFloat2(edit, f.name, f.member, speed, f.attrs.min, f.attrs.max);
        }
        else if constexpr (std::is_same_v<M, glm::vec3>) {
            EditWidgets::DragFloat3(edit, f.name, f.member, speed, f.attrs.min, f.attrs.max);
        }
        else if constexpr (std::is_same_v<M, glm::vec4>) {
            EditWidgets::ColorEdit4(edit, f.name, f.member);
        }
        else if constexpr (std::is_same_v<M, glm::quat>) {
            const glm::quat& current = edit.Get<C>().*f.member;
            glm::vec3 euler = Core::Math::EulerDegrees(current);
            if (ImGui::DragFloat3(f.name, &euler.x, 0.5f, 0.0f, 0.0f, EditWidgets::MixedFormat(edit.IsMixed(f.member), "%.1f"))) {
                edit.PreviewSet(f.member, glm::quat(glm::radians(euler)));
            }
            EditWidgets::CommitOnRelease<C>(edit, false);
        }
        else if constexpr (std::is_enum_v<M>) {
            int32_t value = static_cast<int32_t>(edit.Get<C>().*f.member);
            if (ImGui::InputInt(f.name, &value)) {
                edit.Set(f.member, static_cast<M>(value));
            }
        }
        else if constexpr (IdLike<M>) {
            ImGui::TextDisabled("%s: %" PRIu64, f.name, static_cast<uint64_t>((edit.Get<C>().*f.member).id));
        }
        else if constexpr (requires { EditWidgets::InputText(edit, f.name, f.member); }) {
            EditWidgets::InputText(edit, f.name, f.member);
        }
        else {
            ImGui::TextDisabled("%s", f.name);
        }
    });
}

template<Reflected C>
ComponentEditorResult DrawReflectedComponentEditor(EditContext& edit, const char* name)
{
    const bool bOpen = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    const bool bRemove = ImGui::SmallButton(Core::InlineString<96>::Format("X##%s", name).c_str());
    ImGui::PopStyleColor();
    if (bOpen) {
        ImGui::PushID(name);
        DrawReflectedFields<C>(edit);
        ImGui::PopID();
    }
    return {.bRequestRemoval = bRemove};
}
}

#endif //WILL_ENGINE_REFLECTED_INSPECTOR_H
