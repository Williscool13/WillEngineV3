//
// Created by William on 2026-07-13.
//

#include "editor_multi_edit.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Game::MultiEdit
{
FieldResult ScalarField(const char* id, ImU32 axisColor, float uniformValue, bool mixed, float dragSpeed, float width)
{
    FieldResult result;
    ImGui::PushID(id);

    const float fieldH = ImGui::GetFrameHeight();
    constexpr float stripW = 7.0f;

    ImGui::InvisibleButton("##drag", ImVec2(stripW, fieldH));
    const bool dragActive = ImGui::IsItemActive();
    if (dragActive || ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, axisColor, ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);
    if (dragActive) {
        const float d = ImGui::GetIO().MouseDelta.x * dragSpeed;
        if (d != 0.0f) {
            result.action = FieldAction::Drag;
            result.dragDelta = d;
        }
    }

    ImGui::SameLine(0, 0);

    char buf[64] = {};
    if (!mixed) {
        const auto text = Core::InlineString<64>::Format("%g", uniformValue);
        memcpy(buf, text.c_str(), text.Size() + 1);
    }

    ImGui::SetNextItemWidth(width - stripW);
    if (ImGui::InputTextWithHint("##val", mixed ? "..." : "", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        result.action = FieldAction::Commit;
        memcpy(result.expr, buf, sizeof(result.expr));
        result.expr[sizeof(result.expr) - 1] = '\0';
    }

    ImGui::PopID();
    return result;
}

static void SkipWs(const char*& p)
{
    while (*p == ' ' || *p == '\t') { ++p; }
}

static float RandomFloat(float a, float b, std::mt19937_64& rng)
{
    if (!(b > a)) { return a; }
    std::uniform_real_distribution<float> dist(a, b);
    return dist(rng);
}

static int RandomInt(int a, int b, std::mt19937_64& rng)
{
    if (b <= a) { return a; }
    std::uniform_int_distribution<int> dist(a, b - 1);
    return dist(rng);
}

/** Parses `R(a,b)` starting at p (positioned on the paren); a and b are plain literals. */
static bool ParseRandomArgs(const char*& p, float& a, float& b)
{
    SkipWs(p);
    if (*p != '(') { return false; }
    ++p;
    char* end = nullptr;
    a = strtof(p, &end);
    if (end == p) { return false; }
    p = end;
    SkipWs(p);
    if (*p != ',') { return false; }
    ++p;
    b = strtof(p, &end);
    if (end == p) { return false; }
    p = end;
    SkipWs(p);
    if (*p != ')') { return false; }
    ++p;
    return true;
}

static bool ParseTerm(const char*& p, float x, int index, std::mt19937_64& rng, float& out)
{
    SkipWs(p);
    if (*p == 'x' || *p == 'X') {
        out = x;
        ++p;
        return true;
    }
    if (*p == 's' || *p == 'S') {
        out = static_cast<float>(index);
        ++p;
        return true;
    }
    if (*p == 'r' || *p == 'R') {
        ++p;
        float a, b;
        if (!ParseRandomArgs(p, a, b)) { return false; }
        out = RandomFloat(a, b, rng);
        return true;
    }
    char* end = nullptr;
    const float v = strtof(p, &end);
    if (end == p) { return false; }
    p = end;
    out = v;
    return true;
}

static bool ApplyOp(char op, float lhs, float rhs, float& out)
{
    switch (op) {
        case '+': out = lhs + rhs; return true;
        case '-': out = lhs - rhs; return true;
        case '*': out = lhs * rhs; return true;
        case '/':
            if (rhs == 0.0f) { return false; }
            out = lhs / rhs;
            return true;
        default: return false;
    }
}

bool EvaluateFloatField(const char* expr, float currentValue, int index, std::mt19937_64& rng, float& out)
{
    const char* p = expr;
    SkipWs(p);
    if (*p == '\0') { return false; }

    float lhs;
    if (*p == '*' || *p == '/') {
        lhs = currentValue;
    }
    else if (!ParseTerm(p, currentValue, index, rng, lhs)) {
        return false;
    }

    SkipWs(p);
    if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
        const char op = *p++;
        float rhs;
        if (!ParseTerm(p, currentValue, index, rng, rhs)) { return false; }
        if (!ApplyOp(op, lhs, rhs, lhs)) { return false; }
    }

    SkipWs(p);
    if (*p != '\0') { return false; }

    out = lhs;
    return true;
}

static bool IsWordChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool ContainsNameToken(const char* s)
{
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p == '_') {
            const char c = p[1];
            if (c == 'S' || c == 's' || c == 'R' || c == 'r') { return true; }
        }
    }
    return false;
}

void ExpandNameTemplate(Core::InlineString<128>& dst, const char* templ, int index, std::mt19937_64& rng)
{
    dst.Clear();
    const char* p = templ;
    while (*p != '\0') {
        if (*p == '_') {
            const char c = p[1];
            if ((c == 'S' || c == 's') && !IsWordChar(p[2])) {
                dst.Append("_", 1);
                dst.Append(index);
                p += 2;
                continue;
            }
            if (c == 'R' || c == 'r') {
                const char* q = p + 2;
                float a, b;
                if (ParseRandomArgs(q, a, b)) {
                    dst.Append("_", 1);
                    dst.Append(RandomInt(static_cast<int>(a), static_cast<int>(b), rng));
                    p = q;
                    continue;
                }
            }
            dst.Append("_", 1);
            ++p;
            continue;
        }

        const char* start = p;
        while (*p != '\0' && *p != '_') { ++p; }
        dst.Append(start, static_cast<size_t>(p - start));
    }
}
}
