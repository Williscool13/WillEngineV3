//
// Created by William on 2026-03-21.
//

#include "procedural_mesh_component.h"

#include "spline_mesh_component.h"
#include "static_mesh_component.h"
#include "text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/editor/editor_systems.h"
#include "game/systems/scene_system.h"

namespace Game::Component
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
    auto* state = registry.ctx().get<Engine::EngineState*>();

    registry.remove<MeshRuntime>(entity);

    registry.remove<ProceduralMeshLoadingTag>(entity);
    if (!std::holds_alternative<std::monostate>(component.params)) {
        registry.emplace_or_replace<ProceduralMeshLoadPendingTag>(entity);
        state->assetLoad.bPendingModelResolve |= true;
    }
    else {
        registry.remove<ProceduralMeshLoadPendingTag>(entity);
    }

    auto* transform = registry.try_get<TransformComponent>(entity);
    glm::mat4 m = transform ? GetMatrix(*transform) : glm::mat4(1.0f);
    auto& rt = registry.emplace_or_replace<RenderTransformComponent>(entity, m, m);
    rt.renderOffset = component.renderOffset;
    rt.renderRotation = component.renderRotation;
    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
}
}

namespace Game
{
bool Component::ProceduralMeshComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return !registry.any_of<Component::StaticMeshComponent, Component::SplineMeshComponent, Component::Text3DComponent>(entity);
}

void Component::ProceduralMeshComponent::Serialize(const ProceduralMeshComponent& comp, Engine::TextWriter& w)
{
    static const ProceduralMeshComponent DEF{};
    w.Key("type", static_cast<uint32_t>(comp.params.index()));
    w.Key("material", comp.material.id);
    w.KeyOpt("renderOffset", comp.renderOffset, DEF.renderOffset);
    w.KeyOpt("renderRotation", comp.renderRotation, DEF.renderRotation);

    SerializeProceduralShape(comp.params, w);
}

void Component::SerializeProceduralShape(const Engine::ProceduralParams& params, Engine::TextWriter& w)
{
    std::visit([&w](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
            w.Key("stepCount", p.stepCount);
            w.Key("width", p.width);
            w.Key("totalDepth", p.totalDepth);
            w.Key("totalHeight", p.totalHeight);
            w.Key("bSpecifyStepHeight", p.bSpecifyStepHeight);
            w.Key("stepHeight", p.stepHeight);
            w.Key("bIsClosed", p.bIsClosed);
        }
        else if constexpr (std::is_same_v<T, Engine::BoxParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeY", p.sizeY);
            w.Key("sizeZ", p.sizeZ);
        }
        else if constexpr (std::is_same_v<T, Engine::CylinderParams>) {
            w.Key("radius", p.radius);
            w.Key("height", p.height);
            w.Key("slices", p.slices);
            w.Key("bCapped", p.bCapped);
        }
        else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
            w.Key("radius", p.radius);
            w.Key("height", p.height);
            w.Key("slices", p.slices);
            w.Key("rings", p.rings);
        }
        else if constexpr (std::is_same_v<T, Engine::TorusParams>) {
            w.Key("ringRadius", p.ringRadius);
            w.Key("tubeRadius", p.tubeRadius);
            w.Key("slices", p.slices);
            w.Key("stacks", p.stacks);
        }
        else if constexpr (std::is_same_v<T, Engine::ArchParams>) {
            w.Key("width", p.width);
            w.Key("height", p.height);
            w.Key("depth", p.depth);
            w.Key("thickness", p.thickness);
            w.Key("sides", p.sides);
            w.Key("bFillCorners", p.bFillCorners);
        }
        else if constexpr (std::is_same_v<T, Engine::WedgeParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeY", p.sizeY);
            w.Key("sizeZ", p.sizeZ);
        }
        else if constexpr (std::is_same_v<T, Engine::ConeParams>) {
            w.Key("radius", p.radius);
            w.Key("height", p.height);
            w.Key("slices", p.slices);
            w.Key("bCapped", p.bCapped);
        }
        else if constexpr (std::is_same_v<T, Engine::DoorParams>) {
            w.Key("width", p.width);
            w.Key("height", p.height);
            w.Key("depth", p.depth);
            w.Key("archHeight", p.archHeight);
            w.Key("gap", p.gap);
            w.Key("sides", p.sides);
            w.Key("bHalf", p.bHalf);
            w.Key("bFlip", p.bFlip);
        }
        else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeZ", p.sizeZ);
            w.Key("tilesX", p.tilesX);
            w.Key("tilesZ", p.tilesZ);
        }
        else if constexpr (std::is_same_v<T, Engine::SphereParams>) {
            w.Key("radius", p.radius);
            w.Key("slices", p.slices);
            w.Key("stacks", p.stacks);
        }
        else if constexpr (std::is_same_v<T, Engine::SubdividedSphereParams>) {
            w.Key("radius", p.radius);
            w.Key("subdivisions", p.subdivisions);
        }
        else if constexpr (std::is_same_v<T, Engine::HemisphereParams>) {
            w.Key("radius", p.radius);
            w.Key("slices", p.slices);
            w.Key("stacks", p.stacks);
        }
        else if constexpr (std::is_same_v<T, Engine::PipeParams>) {
            w.Key("outerRadius", p.outerRadius);
            w.Key("innerRadius", p.innerRadius);
            w.Key("height", p.height);
            w.Key("slices", p.slices);
        }
        else if constexpr (std::is_same_v<T, Engine::TetrahedronParams> ||
                           std::is_same_v<T, Engine::OctahedronParams> ||
                           std::is_same_v<T, Engine::IcosahedronParams> ||
                           std::is_same_v<T, Engine::DodecahedronParams>) {
            w.Key("radius", p.radius);
        }
        else if constexpr (std::is_same_v<T, Engine::KleinBottleParams>) {
            w.Key("scale", p.scale);
            w.Key("slices", p.slices);
            w.Key("stacks", p.stacks);
        }
        else if constexpr (std::is_same_v<T, Engine::TrefoilKnotParams>) {
            w.Key("scale", p.scale);
            w.Key("tubeRadius", p.tubeRadius);
            w.Key("slices", p.slices);
            w.Key("stacks", p.stacks);
        }
        else if constexpr (std::is_same_v<T, Engine::CurvedRampParams>) {
            w.Key("width", p.width);
            w.Key("height", p.height);
            w.Key("radius", p.radius);
            w.Key("segments", p.segments);
            w.Key("bHalfPipe", p.bHalfPipe);
            w.Key("flatLength", p.flatLength);
            w.Key("lipHeight", p.lipHeight);
        }
        else if constexpr (std::is_same_v<T, Engine::BowlParams>) {
            w.Key("radius", p.radius);
            w.Key("height", p.height);
            w.Key("curveRadius", p.curveRadius);
            w.Key("flatRadius", p.flatRadius);
            w.Key("lipHeight", p.lipHeight);
            w.Key("slices", p.slices);
            w.Key("segments", p.segments);
        }
        else if constexpr (std::is_same_v<T, Engine::SpiralStaircaseParams>) {
            w.Key("stepCount", p.stepCount);
            w.Key("stepHeight", p.stepHeight);
            w.Key("totalHeight", p.totalHeight);
            w.Key("bSpecifyStepHeight", p.bSpecifyStepHeight);
            w.Key("outerRadius", p.outerRadius);
            w.Key("centerColumnRadius", p.centerColumnRadius);
            w.Key("treadThickness", p.treadThickness);
            w.Key("degreesPerStep", p.degreesPerStep);
            w.Key("totalSweep", p.totalSweep);
            w.Key("bSpecifyDegreesPerStep", p.bSpecifyDegreesPerStep);
            w.Key("arcSegments", p.arcSegments);
            w.Key("bShowCenterColumn", p.bShowCenterColumn);
            w.Key("bRamp", p.bRamp);
        }
        else if constexpr (std::is_same_v<T, Engine::RingParams>) {
            w.Key("outerRadius", p.outerRadius);
            w.Key("innerRadius", p.innerRadius);
            w.Key("slices", p.slices);
            w.Key("bDoubleSided", p.bDoubleSided);
        }
        else if constexpr (std::is_same_v<T, Engine::WallParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeY", p.sizeY);
            w.Key("sizeZ", p.sizeZ);
            const int32_t n = glm::clamp(p.openingCount, 0, Engine::WallParams::MAX_OPENINGS);
            if (n > 0) {
                w.Count("openings", static_cast<uint32_t>(n));
                for (int32_t i = 0; i < n; i++) {
                    w.BeginBlock("o");
                    w.Key("rect", glm::vec4(p.openings[i].x, p.openings[i].y, p.openings[i].w, p.openings[i].h));
                    w.EndBlock();
                }
            }
        }
        else if constexpr (std::is_same_v<T, Engine::LatticeParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeY", p.sizeY);
            w.Key("sizeZ", p.sizeZ);
            w.Key("chordSize", p.chordSize);
            w.Key("braceSize", p.braceSize);
            w.Key("bayCount", p.bayCount);
            w.Key("pattern", p.pattern);
        }
        else if constexpr (std::is_same_v<T, Engine::CorrugatedPanelParams>) {
            w.Key("sizeX", p.sizeX);
            w.Key("sizeY", p.sizeY);
            w.Key("sizeZ", p.sizeZ);
            w.Key("ribDepth", p.ribDepth);
            w.Key("ribWidth", p.ribWidth);
            w.Key("ribCount", p.ribCount);
        }
    }, params);
}

void Component::ProceduralMeshComponent::Deserialize(ProceduralMeshComponent& comp, const Engine::TextReader& r)
{
    comp.material = Engine::MaterialID(r.U64("material", comp.material.id));
    comp.renderOffset = r.Vec3("renderOffset", comp.renderOffset);
    comp.renderRotation = r.Quat("renderRotation", comp.renderRotation);

    comp.params = DeserializeProceduralShape(r.Int("type", 0), r);
}

Engine::ProceduralParams Component::DeserializeProceduralShape(int32_t type, const Engine::TextReader& r)
{
    Engine::ProceduralParams params{};
    if (type == 1) {
        Engine::StaircaseParams p{};
        p.stepCount = r.Int("stepCount", p.stepCount);
        p.width = r.Float("width", p.width);
        p.totalDepth = r.Float("totalDepth", p.totalDepth);
        p.totalHeight = r.Float("totalHeight", p.totalHeight);
        p.bSpecifyStepHeight = r.Bool("bSpecifyStepHeight", false);
        p.stepHeight = r.Float("stepHeight", p.totalHeight / static_cast<float>(std::max(p.stepCount, 1)));
        p.bIsClosed = r.Bool("bIsClosed", true);
        params = p;
    }
    else if (type == 2) {
        Engine::BoxParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeY = r.Float("sizeY", p.sizeY);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        params = p;
    }
    else if (type == 3) {
        Engine::CylinderParams p{};
        p.radius = r.Float("radius", p.radius);
        p.height = r.Float("height", p.height);
        p.slices = r.Int("slices", p.slices);
        p.bCapped = r.Bool("bCapped", p.bCapped);
        params = p;
    }
    else if (type == 4) {
        Engine::CapsuleParams p{};
        p.radius = r.Float("radius", p.radius);
        p.height = r.Float("height", p.height);
        p.slices = r.Int("slices", p.slices);
        p.rings = r.Int("rings", p.rings);
        params = p;
    }
    else if (type == 5) {
        Engine::TorusParams p{};
        p.ringRadius = r.Float("ringRadius", p.ringRadius);
        p.tubeRadius = r.Float("tubeRadius", p.tubeRadius);
        p.slices = r.Int("slices", p.slices);
        p.stacks = r.Int("stacks", p.stacks);
        params = p;
    }
    else if (type == 6) {
        Engine::ArchParams p{};
        p.width = r.Float("width", p.width);
        p.height = r.Float("height", p.height);
        p.depth = r.Float("depth", p.depth);
        p.thickness = r.Float("thickness", p.thickness);
        p.sides = r.Int("sides", p.sides);
        p.bFillCorners = r.Bool("bFillCorners", false);
        params = p;
    }
    else if (type == 7) {
        Engine::WedgeParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeY = r.Float("sizeY", p.sizeY);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        params = p;
    }
    else if (type == 8) {
        Engine::ConeParams p{};
        p.radius = r.Float("radius", p.radius);
        p.height = r.Float("height", p.height);
        p.slices = r.Int("slices", p.slices);
        p.bCapped = r.Bool("bCapped", p.bCapped);
        params = p;
    }
    else if (type == 9) {
        Engine::DoorParams p{};
        p.width = r.Float("width", p.width);
        p.height = r.Float("height", p.height);
        p.depth = r.Float("depth", p.depth);
        p.archHeight = r.Float("archHeight", 0.5f);
        p.gap = r.Float("gap", 0.0f);
        p.sides = r.Int("sides", p.sides);
        p.bHalf = r.Bool("bHalf", p.bHalf);
        p.bFlip = r.Bool("bFlip", false);
        params = p;
    }
    else if (type == 10) {
        Engine::PlaneParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        p.tilesX = r.Int("tilesX", p.tilesX);
        p.tilesZ = r.Int("tilesZ", p.tilesZ);
        params = p;
    }
    else if (type == 11) {
        Engine::SphereParams p{};
        p.radius = r.Float("radius", p.radius);
        p.slices = r.Int("slices", p.slices);
        p.stacks = r.Int("stacks", p.stacks);
        params = p;
    }
    else if (type == 12) {
        Engine::SubdividedSphereParams p{};
        p.radius = r.Float("radius", p.radius);
        p.subdivisions = glm::clamp(r.Int("subdivisions", p.subdivisions), 0, 4);
        params = p;
    }
    else if (type == 13) {
        Engine::HemisphereParams p{};
        p.radius = r.Float("radius", p.radius);
        p.slices = r.Int("slices", p.slices);
        p.stacks = r.Int("stacks", p.stacks);
        params = p;
    }
    else if (type == 14) {
        Engine::PipeParams p{};
        p.outerRadius = r.Float("outerRadius", p.outerRadius);
        p.innerRadius = r.Float("innerRadius", p.innerRadius);
        p.height = r.Float("height", p.height);
        p.slices = r.Int("slices", p.slices);
        params = p;
    }
    else if (type == 15) {
        Engine::TetrahedronParams p{};
        p.radius = r.Float("radius", p.radius);
        params = p;
    }
    else if (type == 16) {
        Engine::OctahedronParams p{};
        p.radius = r.Float("radius", p.radius);
        params = p;
    }
    else if (type == 17) {
        Engine::IcosahedronParams p{};
        p.radius = r.Float("radius", p.radius);
        params = p;
    }
    else if (type == 18) {
        Engine::DodecahedronParams p{};
        p.radius = r.Float("radius", p.radius);
        params = p;
    }
    else if (type == 19) {
        Engine::KleinBottleParams p{};
        p.scale = r.Float("scale", p.scale);
        p.slices = r.Int("slices", p.slices);
        p.stacks = r.Int("stacks", p.stacks);
        params = p;
    }
    else if (type == 20) {
        Engine::TrefoilKnotParams p{};
        p.scale = r.Float("scale", p.scale);
        p.tubeRadius = r.Float("tubeRadius", p.tubeRadius);
        p.slices = r.Int("slices", p.slices);
        p.stacks = r.Int("stacks", p.stacks);
        params = p;
    }
    else if (type == 21) {
        Engine::CurvedRampParams p{};
        p.width = r.Float("width", p.width);
        p.height = r.Float("height", p.height);
        p.radius = r.Float("radius", p.radius);
        p.segments = r.Int("segments", p.segments);
        p.bHalfPipe = r.Bool("bHalfPipe", false);
        p.flatLength = r.Float("flatLength", 1.0f);
        p.lipHeight = r.Float("lipHeight", 0.02f);
        params = p;
    }
    else if (type == 22) {
        Engine::BowlParams p{};
        p.radius = r.Float("radius", p.radius);
        p.height = r.Float("height", p.height);
        p.curveRadius = r.Float("curveRadius", p.curveRadius);
        p.flatRadius = r.Float("flatRadius", 0.0f);
        p.lipHeight = r.Float("lipHeight", 0.02f);
        p.slices = r.Int("slices", p.slices);
        p.segments = r.Int("segments", p.segments);
        params = p;
    }
    else if (type == 23) {
        Engine::SpiralStaircaseParams p{};
        p.stepCount = r.Int("stepCount", p.stepCount);
        p.stepHeight = r.Float("stepHeight", p.stepHeight);
        p.totalHeight = r.Float("totalHeight", p.stepHeight * static_cast<float>(std::max(p.stepCount, 1)));
        p.bSpecifyStepHeight = r.Bool("bSpecifyStepHeight", false);
        p.outerRadius = r.Float("outerRadius", p.outerRadius);
        p.centerColumnRadius = r.Float("centerColumnRadius", p.centerColumnRadius);
        p.treadThickness = r.Float("treadThickness", 0.08f);
        p.degreesPerStep = r.Float("degreesPerStep", 30.0f);
        p.totalSweep = r.Float("totalSweep", p.degreesPerStep * static_cast<float>(std::max(p.stepCount, 1)));
        p.bSpecifyDegreesPerStep = r.Bool("bSpecifyDegreesPerStep", false);
        p.arcSegments = r.Int("arcSegments", 6);
        p.bShowCenterColumn = r.Bool("bShowCenterColumn", true);
        p.bRamp = r.Bool("bRamp", false);
        params = p;
    }
    else if (type == 24) {
        Engine::RingParams p{};
        p.outerRadius = r.Float("outerRadius", p.outerRadius);
        p.innerRadius = r.Float("innerRadius", p.innerRadius);
        p.slices = r.Int("slices", p.slices);
        p.bDoubleSided = r.Bool("bDoubleSided", true);
        params = p;
    }
    else if (type == 25) {
        Engine::WallParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeY = r.Float("sizeY", p.sizeY);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        r.ForEachRecord("openings", [&](const Engine::TextReader& o) {
            if (p.openingCount >= Engine::WallParams::MAX_OPENINGS) { return; }
            const glm::vec4 rect = o.Vec4("rect");
            p.openings[p.openingCount++] = {rect.x, rect.y, rect.z, rect.w};
        });
        params = p;
    }
    else if (type == 26) {
        Engine::LatticeParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeY = r.Float("sizeY", p.sizeY);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        p.chordSize = r.Float("chordSize", p.chordSize);
        p.braceSize = r.Float("braceSize", p.braceSize);
        p.bayCount = r.Int("bayCount", p.bayCount);
        p.pattern = r.Int("pattern", 0);
        params = p;
    }
    else if (type == 27) {
        Engine::CorrugatedPanelParams p{};
        p.sizeX = r.Float("sizeX", p.sizeX);
        p.sizeY = r.Float("sizeY", p.sizeY);
        p.sizeZ = r.Float("sizeZ", p.sizeZ);
        p.ribDepth = r.Float("ribDepth", p.ribDepth);
        p.ribWidth = r.Float("ribWidth", p.ribWidth);
        p.ribCount = r.Int("ribCount", p.ribCount);
        params = p;
    }
    return params;
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
        spline.points.PushBack(glm::vec3{radius * cosf(a), h, radius * sinf(a)});
        spline.rolls.PushBack(0.0f);
    }
    return spline;
}

/** Spawns a child entity (parented to the stair) carrying a railing-mode SplineMeshComponent that traces the helix at `radius`. */
static void CreateSpiralRailingEntity(Engine::EngineState* state, entt::registry& registry, entt::entity stairEntity,
                                      const Engine::SpiralStaircaseParams& p, float radius, float railHeight, const char* suffix)
{
    const entt::entity child = CreateSceneEntity(state);

    if (auto* nm = registry.try_get<Component::NameComponent>(child)) {
        Core::InlineString<256> base("Railing");
        if (const auto* parentName = registry.try_get<Component::NameComponent>(stairEntity)) { base = parentName->name; }
        nm->name = Core::InlineString<256>::Format("%s Railing (%s)", base.c_str(), suffix);
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

Engine::ComponentEditorResult Component::ProceduralMeshComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry,
                                                                              entt::entity entity, const char* name)
{
    static entt::entity editEntity = entt::null;
    static bool bEditingOffset = false;

    if (editEntity != entity) {
        editEntity = entity;
        bEditingOffset = false;
    }

    auto& component = registry.get<ProceduralMeshComponent>(entity);
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto* state = registry.ctx().get<Engine::EngineState*>();

    if (bEditingOffset) { state->editor.bExclusiveGizmoActive = true; }

    bool open = ImGui::CollapsingHeader("Procedural Mesh", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteproceduralmesh");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& renderFlags = registry.get_or_emplace<RenderFlagsComponent>(entity);
        bool visible = renderFlags.Has(RenderFlagsComponent::VISIBLE);
        bool ddgiContribution = renderFlags.Has(RenderFlagsComponent::DDGI_CONTRIBUTE);
        if (ImGui::Checkbox("Visible##proceduralmesh", &visible)) {
            renderFlags.Set(RenderFlagsComponent::VISIBLE, visible);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("DDGI Contribution##proceduralmesh", &ddgiContribution)) {
            renderFlags.Set(RenderFlagsComponent::DDGI_CONTRIBUTE, ddgiContribution);
        }
        bool probeBakeExclude = !renderFlags.Has(RenderFlagsComponent::PROBE_BAKE_INCLUDE);
        if (ImGui::Checkbox("Probe Bake Exclude##proceduralmesh", &probeBakeExclude)) {
            renderFlags.Set(RenderFlagsComponent::PROBE_BAKE_INCLUDE, !probeBakeExclude);
        }

        if (std::holds_alternative<std::monostate>(component.params)) {
            if (ImGui::BeginCombo("Shape", "")) {
                auto selectShape = [&](auto&& params) {
                    component.params = std::move(params);
                    RecreateProceduralMesh(component, registry, entity);
                    modified = true;
                };
                if (ImGui::Selectable("Staircase")) selectShape(Engine::StaircaseParams{});
                if (ImGui::Selectable("Box")) selectShape(Engine::BoxParams{});
                if (ImGui::Selectable("Cylinder")) selectShape(Engine::CylinderParams{});
                if (ImGui::Selectable("Capsule")) selectShape(Engine::CapsuleParams{});
                if (ImGui::Selectable("Torus")) selectShape(Engine::TorusParams{});
                if (ImGui::Selectable("Arch")) selectShape(Engine::ArchParams{});
                if (ImGui::Selectable("Wedge")) selectShape(Engine::WedgeParams{});
                if (ImGui::Selectable("Cone")) selectShape(Engine::ConeParams{});
                if (ImGui::Selectable("Door")) selectShape(Engine::DoorParams{});
                if (ImGui::Selectable("Plane")) selectShape(Engine::PlaneParams{});
                if (ImGui::Selectable("Sphere")) selectShape(Engine::SphereParams{});
                if (ImGui::Selectable("Subdivided Sphere")) selectShape(Engine::SubdividedSphereParams{});
                if (ImGui::Selectable("Hemisphere")) selectShape(Engine::HemisphereParams{});
                if (ImGui::Selectable("Pipe")) selectShape(Engine::PipeParams{});
                if (ImGui::Selectable("Tetrahedron")) selectShape(Engine::TetrahedronParams{});
                if (ImGui::Selectable("Octahedron")) selectShape(Engine::OctahedronParams{});
                if (ImGui::Selectable("Icosahedron")) selectShape(Engine::IcosahedronParams{});
                if (ImGui::Selectable("Dodecahedron")) selectShape(Engine::DodecahedronParams{});
                if (ImGui::Selectable("Klein Bottle")) selectShape(Engine::KleinBottleParams{});
                if (ImGui::Selectable("Trefoil Knot")) selectShape(Engine::TrefoilKnotParams{});
                if (ImGui::Selectable("Curved Ramp")) selectShape(Engine::CurvedRampParams{});
                if (ImGui::Selectable("Bowl")) selectShape(Engine::BowlParams{});
                if (ImGui::Selectable("Spiral Staircase")) selectShape(Engine::SpiralStaircaseParams{});
                if (ImGui::Selectable("Ring")) selectShape(Engine::RingParams{});
                if (ImGui::Selectable("Wall")) selectShape(Engine::WallParams{});
                if (ImGui::Selectable("Lattice")) selectShape(Engine::LatticeParams{});
                if (ImGui::Selectable("Corrugated Panel")) selectShape(Engine::CorrugatedPanelParams{});
                ImGui::EndCombo();
            }
        }
        else {
            static constexpr const char* shapeNames[] = {
                "", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door", "Plane", "Sphere", "Subdivided Sphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
                "Icosahedron", "Dodecahedron", "Klein Bottle", "Trefoil Knot", "Curved Ramp", "Bowl", "Spiral Staircase", "Ring", "Wall", "Lattice", "Corrugated Panel"
            };
            ImGui::Text("Shape: %s", shapeNames[component.params.index()]);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::SmallButton("X##deselect_shape")) {
                component.params = std::monostate{};
                RecreateProceduralMesh(component, registry, entity);
                modified = true;
            }
            ImGui::PopStyleColor();

            bool dirty = false;
            std::visit([&dirty, state, &registry, entity]<typename Shape>(Shape& p) {
                using T = std::decay_t<Shape>;
                if constexpr (std::is_same_v<T, Engine::StaircaseParams>) {
                    ImGui::DragFloat("Width", &p.width, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::DragFloat("Total Depth", &p.totalDepth, 0.01f, 0.01f, 100.0f);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();

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
                    ImGui::DragInt("Rib Count", &p.ribCount, 1, 1, 128);
                    dirty |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }, component.params);

            if (dirty) {
                RecreateProceduralMesh(component, registry, entity);
                modified = true;
            }
        }

        // Material selector
        {
            const char* currentLabel = "(none)";
            if (component.material.IsValid()) {
                if (const Engine::Material* m = ctx->materialManager->GetMaterial(component.material)) {
                    currentLabel = m->name.c_str();
                }
            }
            if (ImGui::BeginCombo("Material", currentLabel)) {
                if (ImGui::Selectable("(none)", !component.material.IsValid())) {
                    if (component.material.IsValid()) {
                        component.material = Engine::MaterialID{};
                        registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
                        state->assetLoad.bPendingModelResolve |= true;
                        modified = true;
                    }
                }
                for (const auto& [matId, mat] : ctx->materialManager->GetMaterials()) {
                    if (mat.immutable) continue;
                    if (ImGui::Selectable(mat.name.c_str(), matId == component.material)) {
                        if (matId != component.material) {
                            component.material = matId;
                            registry.emplace_or_replace<ProceduralMeshLoadingTag>(entity);
                            state->assetLoad.bPendingModelResolve |= true;
                            modified = true;
                        }
                    }
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
        if (drawXYZ("##rox", "##roy", "##roz", &component.renderOffset.x, 0.1f, bEditingOffset)) {
            modified = true;
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderOffset = component.renderOffset;
                registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
            }
        }

        // Rotation row
        glm::vec3 renderEuler = glm::degrees(glm::eulerAngles(component.renderRotation));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::SameLine(labelColW);
        if (drawXYZ("##rrx", "##rry", "##rrz", &renderEuler.x, 0.5f, bEditingOffset)) {
            modified = true;
            component.renderRotation = glm::quat(glm::radians(renderEuler));
            auto* rt = registry.try_get<RenderTransformComponent>(entity);
            if (rt) {
                rt->renderRotation = component.renderRotation;
                registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Button, bEditingOffset ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditingOffset);
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
                modified = true;
                component.renderOffset = Vec3(entityMatInv * Vec4(Vec3(gizmoMat[3]), 1.0f));
                component.renderRotation = glm::inverse(world.rotation) * glm::quat_cast(Mat3(gizmoMat));
                auto* rt = registry.try_get<RenderTransformComponent>(entity);
                if (rt) {
                    rt->renderOffset = component.renderOffset;
                    rt->renderRotation = component.renderRotation;
                    registry.emplace_or_replace<MultiframeDirtyTransformComponent>(entity);
                }
            }
            if (ImGuizmo::IsOver() || ImGuizmo::IsUsing()) { state->editor.bExclusiveGizmoActive = true; }
            ImGuizmo::PopID();
            ImGuizmo::SetGizmoSizeClipSpace(0.1f);
        }
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}

} // Game