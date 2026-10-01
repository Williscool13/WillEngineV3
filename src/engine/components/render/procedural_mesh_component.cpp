//
// Created by William on 2026-03-21.
//

#include "procedural_mesh_component.h"

#include "mesh_source_exclusion.h"
#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "text3d_component.h"
#include "core/math/euler.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/editor/editor_gizmo_helpers.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/editor/editor_materials.h"
#include "engine/editor/editor_systems.h"
#include "engine/systems/scene_system.h"

namespace Engine::Component
{
void ProceduralMeshComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    registry.get_or_emplace<RenderFlagsComponent>(entity);
    auto& component = registry.get<ProceduralMeshComponent>(entity);
    RecreateProceduralMesh(component, registry, entity);
}

void ProceduralMeshComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    registry.remove<MeshRuntime>(entity);
    registry.remove<ProceduralMeshLoadPendingTag>(entity);
    registry.remove<ProceduralMeshLoadingTag>(entity);
    registry.remove<RenderTransformComponent>(entity);
}

void RecreateProceduralMesh(ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    registry.remove<ProceduralMeshLoadingTag>(entity);
    if (!std::holds_alternative<std::monostate>(component.params)) {
        registry.emplace_or_replace<ProceduralMeshLoadPendingTag>(entity);
    }
    else {
        registry.remove<MeshRuntime>(entity);
        registry.remove<ProceduralMeshLoadPendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
}
}

namespace Engine
{
bool Component::ProceduralMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return Component::MeshSources::NoneOtherThan<Component::ProceduralMeshComponent>(registry, entity);
}

/**
 * Samples the staircase helix into a control-point spline at the given radius, railHeight above the (continuous) nosing line.
 * Points are in stair-local space; capped at Spline::MaxPoints (subsamples evenly only past that many steps).
 */
static Engine::Spline BuildSpiralRailingSpline(const Engine::SpiralStaircaseParams& p, float radius, float railHeight)
{
    Engine::Spline spline;
    spline.mode = Engine::SplineMode::CatmullRom;
    spline.bClosed = false;

    const int steps = std::max(1, p.stepCount);
    const float stepH = p.bSpecifyStepHeight ? p.stepHeight : p.totalHeight / static_cast<float>(steps);
    const float dStep = glm::radians(p.bSpecifyDegreesPerStep ? p.degreesPerStep : p.totalSweep / static_cast<float>(steps));
    const float heightPerRad = (dStep > 1e-6f) ? stepH / dStep : 0.0f;
    const float totalAngle = static_cast<float>(steps) * dStep;

    // Sample by angular resolution (~1 control point per 6 deg of sweep) so the Catmull-Rom hugs the helix, at least one per step, capped at the spline budget.
    const float totalDeg = glm::degrees(totalAngle);
    const int desired = std::max(steps + 1, static_cast<int>(totalDeg / 6.0f) + 1);
    const int count = std::min(desired, static_cast<int>(Engine::Spline::MaxPoints));
    for (int k = 0; k < count; k++) {
        const float f = (count <= 1) ? 0.0f : static_cast<float>(k) / static_cast<float>(count - 1);
        const float a = f * totalAngle;
        const float h = a * heightPerRad + railHeight;
        spline.points.PushBack({glm::vec3{radius * cosf(a), h, radius * sinf(a)}});
    }
    return spline;
}

/** Spawns a child entity (parented to the stair) carrying a railing-mode SplineMeshComponent that traces the helix at `radius`. */
static void CreateSpiralRailingEntity(Engine::EngineState* state, entt::registry& registry, entt::entity stairEntity,
                                      const Engine::SpiralStaircaseParams& p, float radius, float railHeight, const char* suffix)
{
    const entt::entity child = CreateSceneEntity(state);

    if (auto* nm = registry.try_get<Component::NameComponent>(child)) {
        Core::InlineString<128> base("Railing");
        if (const auto* parentName = registry.try_get<Component::NameComponent>(stairEntity)) { base = parentName->name; }
        nm->name = Core::InlineString<128>::Format("%s Railing (%s)", base.c_str(), suffix);
    }

    SetParent(state, child, stairEntity);

    // Child sits at the stair's local origin so the local-space railing spline aligns with the stair geometry.
    auto& ct = registry.get<Component::TransformComponent>(child);
    ct.translation = glm::vec3{0.0f};
    ct.rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    ct.scale = glm::vec3{1.0f};
    registry.emplace_or_replace<Component::DirtyTransformTag>(child);

    Component::SplineMeshComponent rc{};
    rc.spline = BuildSpiralRailingSpline(p, radius, railHeight);
    rc.radius = 0.04f;
    rc.sides = 8;
    rc.segmentsPerSpan = 8;
    rc.bCaps = true;
    rc.profile.type = Engine::SplineProfileType::Tube;
    rc.railing.bEnabled = true;
    rc.railing.lanes.Clear();
    rc.railing.lanes.PushBack(glm::vec2{0.0f, 0.0f});
    rc.railing.bPosts = true;
    rc.railing.postBottom = -railHeight;
    rc.railing.postTop = 0.0f;
    rc.railing.postSize = glm::vec2{0.03f, 0.03f};
    rc.railing.postInterval = rc.segmentsPerSpan;
    registry.emplace<Component::SplineMeshComponent>(child, std::move(rc));

    MarkSceneModified(state, state->scene.currentSceneId);
}

void Component::ProceduralMeshComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    const auto& component = registry.get<ProceduralMeshComponent>(entity);
    if (auto* rt = registry.try_get<RenderTransformComponent>(entity)) {
        rt->renderOffset = component.renderOffset;
        rt->renderRotation = component.renderRotation;
        registry.emplace_or_replace<MultiframeDirtyComponent>(entity);
    }
}

void Component::ProceduralMeshComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    RecreateProceduralMesh(registry.get<ProceduralMeshComponent>(entity), registry, entity);
}

template<size_t... I>
static Engine::ProceduralParams DefaultProceduralParams(size_t index, std::index_sequence<I...>)
{
    Engine::ProceduralParams params;
    ((I == index ? (params.emplace<I>(), 0) : 0), ...);
    return params;
}

/** Depth drag, edge presets and per-edge values for a box-layout chamfer set. */
static bool DrawChamferEditor(float* chamferX, float* chamferY, float* chamferZ)
{
    bool dirty = false;
    float* edges[3] = {chamferX, chamferY, chamferZ};
    float depth = 0.0f;
    for (int32_t c = 0; c < 3; c++) {
        for (int32_t i = 0; i < 4; i++) { depth = glm::max(depth, edges[c][i]); }
    }
    const bool bAnyChamfered = depth > 0.0f;
    if (ImGui::DragFloat("Chamfer", &depth, 0.002f, 0.0f, 10.0f, "%.3f")) {
        for (int32_t c = 0; c < 3; c++) {
            for (int32_t i = 0; i < 4; i++) {
                if (!bAnyChamfered || edges[c][i] > 0.0f) { edges[c][i] = depth; }
            }
        }
    }
    dirty |= ImGui::IsItemDeactivatedAfterEdit();

    auto applyPreset = [&](uint32_t mask) {
        const float presetDepth = depth > 0.0f ? depth : 0.02f;
        for (int32_t c = 0; c < 3; c++) {
            for (int32_t i = 0; i < 4; i++) { edges[c][i] = (mask >> (c * 4 + i)) & 1u ? presetDepth : 0.0f; }
        }
        dirty = true;
    };
    if (ImGui::SmallButton("All")) { applyPreset(0xFFFu); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Top")) { applyPreset(0xAu | (0xCu << 8)); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Bottom")) { applyPreset(0x5u | (0x3u << 8)); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Vertical")) { applyPreset(0xFu << 4); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) { applyPreset(0u); }

    if (ImGui::TreeNode("Chamfer Per Edge")) {
        ImGui::DragFloat4("X Edges", chamferX, 0.002f, 0.0f, 10.0f, "%.3f");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat4("Y Edges", chamferY, 0.002f, 0.0f, 10.0f, "%.3f");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat4("Z Edges", chamferZ, 0.002f, 0.0f, 10.0f, "%.3f");
        dirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TreePop();
    }
    return dirty;
}

Engine::ComponentEditorResult Component::ProceduralMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    static entt::entity editEntity = entt::null;
    static bool bEditingOffset = false;

    if (editEntity != entity || edit.IsMulti()) {
        editEntity = entity;
        bEditingOffset = false;
    }

    const ProceduralMeshComponent before = edit.Get<ProceduralMeshComponent>();
    ProceduralMeshComponent component = before;
    bool bCommit = false;
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = edit.State();

    if (bEditingOffset) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Procedural Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteproceduralmesh");
    ImGui::PopStyleColor();

    if (open) {
        if (DrawRenderFlagToggles(edit, RENDER_TOGGLE_ALL)) {
            edit.ForEachTarget<ProceduralMeshComponent>([&registry](entt::entity e) { registry.emplace_or_replace<ProceduralMeshLoadingTag>(e); });
        }

        static constexpr const char* shapeNames[] = {
            "", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
            "Icosahedron", "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl", "Spiral Staircase", "Ring", "Wall", "Lattice", "Corrugated Panel", "Terrace", "Pyramid",
            "Slanted Beam"
        };
        static_assert(std::size(shapeNames) == std::variant_size_v<Engine::ProceduralParams>);
        const size_t shapeIndex = component.params.index();
        if (ImGui::BeginCombo("Shape", shapeNames[shapeIndex])) {
            for (size_t i = 1; i < std::size(shapeNames); ++i) {
                if (ImGui::Selectable(shapeNames[i], i == shapeIndex) && i != shapeIndex) {
                    component.params = DefaultProceduralParams(i, std::make_index_sequence<std::variant_size_v<Engine::ProceduralParams>>{});
                    bCommit = true;
                }
            }
            ImGui::EndCombo();
        }

        if (!std::holds_alternative<std::monostate>(component.params)) {
            bool dirty = false;
            std::visit([&dirty, state, &registry, entity]<typename Shape>(Shape& p) {
                using T = std::decay_t<Shape>;
                if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (p.bSpecifyStepDepth) {
                        float derivedDepth = Engine::StaircaseTotalDepth(p);
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Total Depth", &derivedDepth, 0.01f, 0.01f, 100.0f);
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::DragFloat("Total Depth", &p.totalDepth, 0.01f, 0.01f, 100.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (ImGui::Checkbox("Specify Step Depth", &p.bSpecifyStepDepth)) {
                        if (p.bSpecifyStepDepth) {
                            p.stepDepth = p.totalDepth / static_cast<float>(std::max(p.stepCount, 1));
                        }
                        else {
                            p.totalDepth = p.stepDepth * static_cast<float>(std::max(p.stepCount, 1));
                        }
                        dirty = true;
                    }
                    if (p.bSpecifyStepDepth) {
                        ImGui::DragFloat("Step Depth", &p.stepDepth, 0.001f, 0.001f, 10.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    else {
                        float derivedStepDepth = p.totalDepth / static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Depth", &derivedStepDepth, 0.001f, 0.001f, 10.0f);
                        ImGui::EndDisabled();
                    }

                    // Total Height — always editable; drives stepCount when bSpecifyStepHeight
                    ImGui::DragFloat("Total Height", &p.totalHeight, 0.01f, 0.01f, 100.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        if (p.bSpecifyStepHeight) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                        }
                        dirty = true;
                    }

                    if (ImGui::Checkbox("Specify Step Height", &p.bSpecifyStepHeight)) {
                        if (p.bSpecifyStepHeight) {
                            p.stepHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        }
                    }

                    // Step Count — editable when !bSpecifyStepHeight, greyed float when derived
                    if (p.bSpecifyStepHeight) {
                        float derivedCount = p.totalHeight / std::max(p.stepHeight, 0.001f);
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Count", &derivedCount, 1.0f, 1.0f, 256.0f, "%.2f");
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::DragInt("Step Count", &p.stepCount, 1, 1, 256);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }

                    // Step Height — editable when bSpecifyStepHeight, greyed derived otherwise
                    if (p.bSpecifyStepHeight) {
                        ImGui::DragFloat("Step Height", &p.stepHeight, 0.001f, 0.001f, 10.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                            dirty = true;
                        }
                    }
                    else {
                        float derivedHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Height", &derivedHeight, 0.001f, 0.001f, 10.0f);
                        ImGui::EndDisabled();
                    }

                    if (ImGui::Checkbox("Closed", &p.bIsClosed)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::BoxParams>) {
                    const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
                    const float outerSpacing = ImGui::GetStyle().ItemSpacing.x;
                    const float frameRounding = ImGui::GetStyle().FrameRounding;
                    const float fieldH = ImGui::GetFrameHeight();
                    const float labelColW = ImGui::CalcTextSize("Size").x + outerSpacing * 3.0f;
                    const float fieldW = (ImGui::GetContentRegionAvail().x - labelColW - innerSpacing * 2.0f) / 3.0f;
                    constexpr float stripW = 4.0f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    auto drawSizeField = [&](const char* id, float* val, ImU32 strip) {
                        ImGui::SetNextItemWidth(fieldW);
                        ImGui::DragFloat(id, val, 0.01f, 0.01f, 100.0f, "%.2f");
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                        ImVec2 p = ImGui::GetItemRectMin();
                        dl->AddRectFilled(p, {p.x + stripW, p.y + fieldH}, strip, frameRounding, ImDrawFlags_RoundCornersLeft);
                    };

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Size");
                    ImGui::SameLine(labelColW);
                    drawSizeField("##bsx", &p.sizeX, Editor::COLOR_AXIS_X); ImGui::SameLine(0, innerSpacing);
                    drawSizeField("##bsy", &p.sizeY, Editor::COLOR_AXIS_Y); ImGui::SameLine(0, innerSpacing);
                    drawSizeField("##bsz", &p.sizeZ, Editor::COLOR_AXIS_Z);

                    dirty |= DrawChamferEditor(p.chamferX, p.chamferY, p.chamferZ);
                }
                else if constexpr (std::is_same_v<T, Engine::CylinderParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Capped", &p.bCapped)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Rings", &p.rings, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TorusParams>) {
                    ImGui::DragFloat("Ring Radius", &p.ringRadius, 0.01f, 0.01f, 50.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.tubeRadius = glm::clamp(p.tubeRadius, p.ringRadius * 0.1f, p.ringRadius * 0.99f);
                        dirty = true;
                    }
                    ImGui::DragFloat("Tube Radius", &p.tubeRadius, 0.01f, 0.001f, p.ringRadius * 0.99f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.tubeRadius = glm::clamp(p.tubeRadius, p.ringRadius * 0.1f, p.ringRadius * 0.99f);
                        dirty = true;
                    }
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::ArchParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Depth", &p.depth, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Thickness", &p.thickness, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Sides", &p.sides, 1, 1, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Fill Corners", &p.bFillCorners)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::WedgeParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Base Height", &p.baseHeight, 0.005f, 0.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Block under the whole wedge; Size Y is the rise above it"); }
                    dirty |= DrawChamferEditor(p.chamferX, p.chamferY, p.chamferZ);
                }
                else if constexpr (std::is_same_v<T, Engine::ConeParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Capped", &p.bCapped)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::DoorParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Depth", &p.depth, 0.001f, 0.001f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Arch Height", &p.archHeight, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Gap", &p.gap, 0.001f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Sides", &p.sides, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Half", &p.bHalf)) { dirty = true; }
                    ImGui::SameLine();
                    if (ImGui::Checkbox("Flip (right-side hinge)", &p.bFlip)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 1000.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 1000.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Tiles X", &p.tilesX, 1, 1, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Tiles Z", &p.tilesZ, 1, 1, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::SphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::SubdividedSphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Subdivisions", &p.subdivisions, 1, 0, 4);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::HemisphereParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::PipeParams>) {
                    ImGui::DragFloat("Outer Radius", &p.outerRadius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Inner Radius", &p.innerRadius, 0.01f, 0.001f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TetrahedronParams> ||
                                   std::is_same_v<T, Engine::OctahedronParams> ||
                                   std::is_same_v<T, Engine::IcosahedronParams> ||
                                   std::is_same_v<T, Engine::DodecahedronParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 50.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::KleinBottleParams>) {
                    ImGui::TextDisabled("Requires double-sided material");
                    ImGui::DragFloat("Scale", &p.scale, 0.01f, 0.001f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::TrefoilKnotParams>) {
                    ImGui::DragFloat("Scale", &p.scale, 0.01f, 0.001f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Tube Radius", &p.tubeRadius, 0.1f, 0.5f, 3.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Stacks", &p.stacks, 1, 3, 512);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::CurvedRampParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Segments", &p.segments, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Half-Pipe", &p.bHalfPipe)) { dirty = true; }
                    if (p.bHalfPipe) {
                        ImGui::DragFloat("Flat Length", &p.flatLength, 0.01f, 0.0f, 100.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    ImGui::DragFloat("Lip Height", &p.lipHeight, 0.005f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::BowlParams>) {
                    ImGui::DragFloat("Radius", &p.radius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Curve Radius", &p.curveRadius, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Flat Radius", &p.flatRadius, 0.01f, 0.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Lip Height", &p.lipHeight, 0.005f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Segments", &p.segments, 1, 2, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
                else if constexpr (std::is_same_v<T, Engine::SpiralStaircaseParams>) {
                    if (ImGui::Checkbox("Ramp (continuous helix)", &p.bRamp)) { dirty = true; }

                    // Total Height — always editable; drives stepCount when bSpecifyStepHeight
                    ImGui::DragFloat("Total Height", &p.totalHeight, 0.01f, 0.01f, 100.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        if (p.bSpecifyStepHeight) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                        }
                        dirty = true;
                    }

                    if (ImGui::Checkbox("Specify Step Height", &p.bSpecifyStepHeight)) {
                        if (p.bSpecifyStepHeight) {
                            p.stepHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        }
                        dirty = true;
                    }

                    if (p.bSpecifyStepHeight) {
                        float derivedCount = p.totalHeight / std::max(p.stepHeight, 0.001f);
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Count", &derivedCount, 1.0f, 1.0f, 256.0f, "%.2f");
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::DragInt("Step Count", &p.stepCount, 1, 1, 256);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    }

                    if (p.bSpecifyStepHeight) {
                        ImGui::DragFloat("Step Height", &p.stepHeight, 0.001f, 0.001f, 10.0f);
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            p.stepCount = std::max(1, (int32_t) std::ceil(p.totalHeight / std::max(p.stepHeight, 0.001f)));
                            dirty = true;
                        }
                    }
                    else {
                        float derivedHeight = p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Step Height", &derivedHeight, 0.001f, 0.001f, 10.0f);
                        ImGui::EndDisabled();
                    }

                    const float effStepH = p.bSpecifyStepHeight ? p.stepHeight : p.totalHeight / static_cast<float>(std::max(p.stepCount, 1));

                    // Rotation: Total Sweep vs Degrees Per Step. stepCount is owned by the height section, so the toggle only swaps which one is typed.
                    if (ImGui::Checkbox("Specify Degrees Per Step", &p.bSpecifyDegreesPerStep)) {
                        if (p.bSpecifyDegreesPerStep) {
                            p.degreesPerStep = p.totalSweep / static_cast<float>(std::max(p.stepCount, 1));
                        }
                        else {
                            p.totalSweep = p.degreesPerStep * static_cast<float>(std::max(p.stepCount, 1));
                        }
                        dirty = true;
                    }
                    if (p.bSpecifyDegreesPerStep) {
                        ImGui::DragFloat("Degrees Per Step", &p.degreesPerStep, 0.5f, 1.0f, 180.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                        float derivedSweep = p.degreesPerStep * static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Total Sweep (deg)", &derivedSweep, 1.0f, 1.0f, 100000.0f, "%.1f");
                        ImGui::EndDisabled();
                    }
                    else {
                        ImGui::DragFloat("Total Sweep (deg)", &p.totalSweep, 1.0f, 1.0f, 100000.0f);
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                        float derivedDeg = p.totalSweep / static_cast<float>(std::max(p.stepCount, 1));
                        ImGui::BeginDisabled(true);
                        ImGui::DragFloat("Degrees Per Step", &derivedDeg, 0.5f, 1.0f, 180.0f, "%.2f");
                        ImGui::EndDisabled();
                    }

                    ImGui::DragFloat("Outer Radius", &p.outerRadius, 0.01f, 0.05f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Center Column Radius", &p.centerColumnRadius, 0.01f, 0.0f, p.outerRadius - 0.01f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Tread Thickness", &p.treadThickness, 0.005f, 0.001f, effStepH);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Arc Segments", &p.arcSegments, 1, 1, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Show Center Column", &p.bShowCenterColumn)) { dirty = true; }

                    ImGui::SeparatorText("Railing");
                    static int railingSide = 0;
                    static float railingHeight = 1.0f;
                    const char* railingSideNames[] = {"Outer", "Inner", "Both"};
                    ImGui::Combo("Railing Side", &railingSide, railingSideNames, 3);
                    ImGui::DragFloat("Railing Height", &railingHeight, 0.01f, 0.1f, 5.0f);
                    if (ImGui::Button("Create Railing (child entity)")) {
                        if (railingSide == 0 || railingSide == 2) {
                            CreateSpiralRailingEntity(state, registry, entity, p, p.outerRadius, railingHeight, "Outer");
                        }
                        if (railingSide == 1 || railingSide == 2) {
                            CreateSpiralRailingEntity(state, registry, entity, p, std::max(0.02f, p.centerColumnRadius), railingHeight, "Inner");
                        }
                    }
                }
                else if constexpr (std::is_same_v<T, Engine::RingParams>) {
                    ImGui::DragFloat("Outer Radius", &p.outerRadius, 0.01f, 0.01f, 50.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.innerRadius = glm::clamp(p.innerRadius, 0.0f, p.outerRadius - 0.001f);
                        dirty = true;
                    }
                    ImGui::DragFloat("Inner Radius", &p.innerRadius, 0.01f, 0.0f, p.outerRadius - 0.001f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        p.innerRadius = glm::clamp(p.innerRadius, 0.0f, p.outerRadius - 0.001f);
                        dirty = true;
                    }
                    ImGui::DragInt("Slices", &p.slices, 1, 3, 256);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Checkbox("Double Sided", &p.bDoubleSided)) { dirty = true; }
                    if (p.innerRadius <= 1e-4f) { ImGui::TextDisabled("Inner radius 0 -> solid disc"); }
                }
                else if constexpr (std::is_same_v<T, Engine::WallParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();

                    ImGui::SeparatorText("Openings");
                    int removeIdx = -1;
                    for (int32_t i = 0; i < p.openingCount; i++) {
                        ImGui::PushID(i);
                        ImGui::DragFloat4("##opening", &p.openings[i].x, 0.01f, 0.0f, 100.0f, "%.2f");
                        dirty |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) { removeIdx = i; }
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0) {
                        for (int32_t i = removeIdx; i < p.openingCount - 1; i++) { p.openings[i] = p.openings[i + 1]; }
                        p.openings[p.openingCount - 1] = {};
                        p.openingCount--;
                        dirty = true;
                    }
                    if (p.openingCount < Engine::WallParams::MAX_OPENINGS) {
                        if (ImGui::Button("Add Opening")) {
                            p.openings[p.openingCount++] = {p.sizeX * 0.25f, p.sizeY * 0.25f, p.sizeX * 0.5f, p.sizeY * 0.5f};
                            dirty = true;
                        }
                    }
                    ImGui::TextDisabled("x, y, w, h in face plane (X along Size X, Y along Size Y)");
                }
                else if constexpr (std::is_same_v<T, Engine::LatticeParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Chord Size", &p.chordSize, 0.005f, 0.005f, 2.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Brace Size", &p.braceSize, 0.005f, 0.005f, 2.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Bay Count", &p.bayCount, 1, 1, 64);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    const char* patterns[] = {"X-Brace", "Single Diagonal"};
                    if (ImGui::Combo("Pattern", &p.pattern, patterns, 2)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::CorrugatedPanelParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Y", &p.sizeY, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.005f, 0.005f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Rib Depth", &p.ribDepth, 0.005f, 0.0f, 5.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Rib Width", &p.ribWidth, 0.005f, 0.0f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Rib Count", &p.ribCount, 1, 1, 256);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Flank Angle", &p.flankAngle, 0.5f, 0.0f, 89.0f, "%.1f deg");
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rib side angle from vertical; 0 = square fins"); }
                    ImGui::DragInt("Alternate Every", &p.alternateEvery, 1, 0, 16);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Every Nth rib uses Alternate Depth; 0 or 1 = off"); }
                    ImGui::BeginDisabled(p.alternateEvery < 2);
                    ImGui::DragFloat("Alternate Depth", &p.alternateDepth, 0.005f, 0.0f, 5.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::EndDisabled();
                }
                else if constexpr (std::is_same_v<T, Engine::TerraceParams>) {
                    int direction = static_cast<int>(p.direction);
                    const char* directions[] = {"Up", "Down"};
                    if (ImGui::Combo("Direction", &direction, directions, 2)) {
                        p.direction = static_cast<Engine::TerraceDirection>(direction);
                        dirty = true;
                    }
                    int profile = static_cast<int>(p.profile);
                    const char* profiles[] = {"Steps", "Ramp"};
                    if (ImGui::Combo("Profile", &profile, profiles, 2)) {
                        p.profile = static_cast<Engine::TerraceProfile>(profile);
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Ramp slopes through the step nosings; the physics collider keeps its own profile"); }
                    ImGui::TextUnformatted("Steps on");
                    auto sideToggle = [&](const char* label, int32_t bit) {
                        ImGui::SameLine();
                        bool bOn = (p.sides & bit) != 0;
                        if (ImGui::Checkbox(label, &bOn)) {
                            p.sides = bOn ? (p.sides | bit) : (p.sides & ~bit);
                            dirty = true;
                        }
                    };
                    sideToggle("-X", Engine::TERRACE_SIDE_NEG_X);
                    sideToggle("+X", Engine::TERRACE_SIDE_POS_X);
                    sideToggle("-Z", Engine::TERRACE_SIDE_NEG_Z);
                    sideToggle("+Z", Engine::TERRACE_SIDE_POS_Z);
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragInt("Step Count", &p.stepCount, 1, 1, Engine::TerraceParams::MAX_STEPS);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Step Rise", &p.stepRise, 0.005f, 0.0f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Step Run", &p.stepRun, 0.005f, 0.0f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Base Height", &p.baseHeight, 0.005f, 0.0f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Solid under every step; 0 with Down leaves a hole through the middle"); }
                    ImGui::DragFloat("Lip Width", &p.lipWidth, 0.005f, 0.0f, 10.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Extends the outermost level past the footprint on the stepped sides"); }
                    dirty |= ImGui::Checkbox("Floor", &p.bFloor);
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Off removes the innermost level, leaving an open-ended tube"); }
                }
                else if constexpr (std::is_same_v<T, Engine::PyramidParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Size Z", &p.sizeZ, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Height", &p.height, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Top Scale", &p.topScale, 0.005f, 0.0f, 1.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("0 is a pointed apex; above 0 a flat top that fraction of the base"); }
                    if (ImGui::Checkbox("Capped", &p.bCapped)) { dirty = true; }
                }
                else if constexpr (std::is_same_v<T, Engine::SlantedBeamParams>) {
                    ImGui::DragFloat("Size X", &p.sizeX, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Length", &p.length, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Horizontal run along Z"); }
                    ImGui::DragFloat("Rise", &p.rise, 0.01f, -100.0f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Thickness", &p.thickness, 0.005f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Measured vertically; the end faces are this tall"); }
                    dirty |= DrawChamferEditor(p.chamferX, p.chamferY, p.chamferZ);
                }
            }, component.params);

            bCommit |= dirty;
        }

        // Material selector
        {
            const char* currentLabel = "(none)";
            if (component.material.IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.material)) {
                    currentLabel = m->name.c_str();
                }
            }
            if (ImGui::BeginCombo("Material", edit.IsMixed(&ProceduralMeshComponent::material) ? "--" : currentLabel, ImGuiComboFlags_HeightLarge)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    component.material = Engine::MaterialID{};
                    bCommit = true;
                }
                const Engine::MaterialID picked = Engine::DrawMaterialSelector(ctx, state, state->editor.materialSelector, component.material);
                if (picked.IsValid() && picked != component.material) {
                    component.material = picked;
                    bCommit = true;
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SeparatorText("Render Transform");

        const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float outerSpacing = ImGui::GetStyle().ItemSpacing.x;
        const float frameRounding = ImGui::GetStyle().FrameRounding;
        const float labelColW = ImGui::CalcTextSize("Rotation").x + outerSpacing * 3.0f;
        const float fieldW = (ImGui::GetContentRegionAvail().x - labelColW - innerSpacing * 2.0f) / 3.0f;
        const float fieldH = ImGui::GetFrameHeight();
        constexpr float stripW = 4.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto drawField = [&](const char* id, float* val, ImU32 strip, float speed, bool editable) -> bool {
            ImGui::SetNextItemWidth(fieldW);
            ImGui::BeginDisabled(!editable);
            bool changed = ImGui::DragFloat(id, val, speed, 0, 0, "%.2f");
            bCommit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::EndDisabled();
            ImVec2 p = ImGui::GetItemRectMin();
            dl->AddRectFilled(p, {p.x + stripW, p.y + fieldH}, strip, frameRounding, ImDrawFlags_RoundCornersLeft);
            return changed;
        };
        auto drawXYZ = [&](const char* idX, const char* idY, const char* idZ, float* v, float speed, bool editable) -> bool {
            bool c = false;
            c |= drawField(idX, v + 0, Editor::COLOR_AXIS_X, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idY, v + 1, Editor::COLOR_AXIS_Y, speed, editable);
            ImGui::SameLine(0, innerSpacing);
            c |= drawField(idZ, v + 2, Editor::COLOR_AXIS_Z, speed, editable);
            return c;
        };

        // Offset row
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Offset");
        ImGui::SameLine(labelColW);
        drawXYZ("##rox", "##roy", "##roz", &component.renderOffset.x, 0.1f, bEditingOffset);

        // Rotation row
        glm::vec3 renderEuler = Core::Math::EulerDegrees(component.renderRotation);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(labelColW);
        if (drawXYZ("##rrx", "##rry", "##rrz", &renderEuler.x, 0.5f, bEditingOffset)) {
            component.renderRotation = glm::quat(glm::radians(renderEuler));
        }

        ImGui::BeginDisabled(edit.IsMulti());
        bCommit |= Editor::MeshPivotPresets(ctx, registry, entity, component.renderRotation, component.renderOffset);
        ImGui::EndDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, bEditingOffset ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled(edit.IsMulti() || ((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditingOffset));
        if (ImGui::Button(bEditingOffset ? "Done##offsetedit" : "Edit##offsetedit")) {
            bEditingOffset = !bEditingOffset;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    if (bEditingOffset) {
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (transform) {
            const auto& world = registry.get<WorldTransformComponent>(entity);
            const Mat4 entityMat = GetMatrix(world);
            const Mat4 entityMatInv = glm::inverse(entityMat);
            const Vec3 pivotWorld = Vec3(entityMat * Vec4(component.renderOffset, 1.0f));

            const Mat4 view = viewFamily.mainView.currentViewData.view;
            const Mat4 proj = viewFamily.mainView.currentViewData.proj;

            float snapArr[3] = {};
            float* snap = nullptr;
            if (state->editor.bSnapEnabled) {
                if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapTranslation;
                } else if (state->editor.currentGizmoOperation == ImGuizmo::ROTATE) {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapRotation;
                } else {
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapScale;
                }
                snap = snapArr;
            }

            ImGuizmo::SetGizmoSizeClipSpace(0.10f);
            ImGuizmo::PushID(Editor::GizmoId::PROCEDURAL_MESH_TRANSFORM);
            const Quat worldRenderRot = world.rotation * component.renderRotation;
            Mat4 gizmoMat = glm::translate(Mat4(1.0f), pivotWorld) * glm::mat4_cast(worldRenderRot);
            if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), state->editor.currentGizmoOperation, state->editor.currentGizmoMode, glm::value_ptr(gizmoMat), nullptr, snap)) {
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(world.rotation) * glm::quat_cast(Mat3(gizmoMat));
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }
    }

    edit.PreviewDiff(before, component);
    if (bCommit) {
        edit.Commit<ProceduralMeshComponent>();
    }

    return {.bRequestRemoval = remove};
}

} // Engine