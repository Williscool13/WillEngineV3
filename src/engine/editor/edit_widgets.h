//
// Created by William on 2026-09-28.
//

#ifndef WILL_ENGINE_EDIT_WIDGETS_H
#define WILL_ENGINE_EDIT_WIDGETS_H

#include <cstdint>
#include <cstring>
#include <type_traits>

#include <glm/glm.hpp>

#include "imgui.h"
#include "imgui_internal.h"

#include "core/containers/inline_string.h"
#include "engine/editor/edit_context.h"

/** Preview while dragging, commit on release; "--" when the selection disagrees. */
namespace Engine::EditWidgets
{
inline const char* MixedFormat(bool bMixed, const char* format)
{
    return bMixed ? "--" : format;
}

template<typename C>
bool CommitOnRelease(EditContext& edit, bool bChanged)
{
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        edit.Commit<C>();
    }
    return bChanged;
}

template<typename C>
bool DragFloat(EditContext& edit, const char* label, float C::* member, float speed = 0.01f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f")
{
    float v = edit.Get<C>().*member;
    const bool bChanged = ImGui::DragFloat(label, &v, speed, min, max, MixedFormat(edit.IsMixed(member), format));
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool SliderFloat(EditContext& edit, const char* label, float C::* member, float min, float max, const char* format = "%.3f")
{
    float v = edit.Get<C>().*member;
    const bool bChanged = ImGui::SliderFloat(label, &v, min, max, MixedFormat(edit.IsMixed(member), format));
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C, typename I> requires std::is_integral_v<I>
bool DragInt(EditContext& edit, const char* label, I C::* member, float speed = 1.0f, int32_t min = 0, int32_t max = 0)
{
    int32_t v = static_cast<int32_t>(edit.Get<C>().*member);
    const bool bChanged = ImGui::DragInt(label, &v, speed, min, max, MixedFormat(edit.IsMixed(member), "%d"));
    if (bChanged) {
        edit.PreviewSet(member, static_cast<I>(v));
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool DragFloat2(EditContext& edit, const char* label, glm::vec2 C::* member, float speed = 0.01f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f")
{
    glm::vec2 v = edit.Get<C>().*member;
    const bool bChanged = ImGui::DragFloat2(label, &v.x, speed, min, max, MixedFormat(edit.IsMixed(member), format));
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool DragFloat3(EditContext& edit, const char* label, glm::vec3 C::* member, float speed = 0.01f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f")
{
    glm::vec3 v = edit.Get<C>().*member;
    const bool bChanged = ImGui::DragFloat3(label, &v.x, speed, min, max, MixedFormat(edit.IsMixed(member), format));
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool ColorEdit3(EditContext& edit, const char* label, glm::vec3 C::* member, ImGuiColorEditFlags flags = 0)
{
    glm::vec3 v = edit.Get<C>().*member;
    const bool bChanged = ImGui::ColorEdit3(label, &v.x, flags);
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool ColorEdit4(EditContext& edit, const char* label, glm::vec4 C::* member, ImGuiColorEditFlags flags = 0)
{
    glm::vec4 v = edit.Get<C>().*member;
    const bool bChanged = ImGui::ColorEdit4(label, &v.x, flags);
    if (bChanged) {
        edit.PreviewSet(member, v);
    }
    return CommitOnRelease<C>(edit, bChanged);
}

template<typename C>
bool Checkbox(EditContext& edit, const char* label, bool C::* member)
{
    bool v = edit.Get<C>().*member;
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, edit.IsMixed(member));
    const bool bChanged = ImGui::Checkbox(label, &v);
    ImGui::PopItemFlag();
    if (bChanged) {
        edit.Set(member, v);
    }
    return bChanged;
}

template<typename C, typename E>
bool Combo(EditContext& edit, const char* label, E C::* member, const char* const items[], int32_t count)
{
    int32_t v = static_cast<int32_t>(edit.Get<C>().*member);
    const bool bMixed = edit.IsMixed(member);
    const char* preview = bMixed ? "--" : (v >= 0 && v < count ? items[v] : "?");
    bool bChanged = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (int32_t i = 0; i < count; ++i) {
            if (ImGui::Selectable(items[i], !bMixed && i == v)) {
                edit.Set(member, static_cast<E>(i));
                bChanged = true;
            }
        }
        ImGui::EndCombo();
    }
    return bChanged;
}

template<typename C, size_t N>
bool InputText(EditContext& edit, const char* label, Core::InlineString<N> C::* member, ImGuiInputTextFlags flags = 0)
{
    const Core::InlineString<N>& current = edit.Get<C>().*member;
    char buf[N + 1];
    const size_t len = current.Size() < N ? current.Size() : N;
    memcpy(buf, current.c_str(), len);
    buf[len] = '\0';
    const bool bChanged = ImGui::InputText(label, buf, sizeof(buf), flags);
    if (bChanged) {
        edit.PreviewSet(member, Core::InlineString<N>(buf));
    }
    return CommitOnRelease<C>(edit, bChanged);
}
}

#endif //WILL_ENGINE_EDIT_WIDGETS_H
