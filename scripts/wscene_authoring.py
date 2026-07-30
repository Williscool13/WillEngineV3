#!/usr/bin/env python3
"""
Helper library for hand-authoring .wscene files without parsing an existing
scene's JSON by hand each time. Component-type keys are computed, not copied:
they are fnv1a64 of the name each component is registered under in
component_registry.cpp (see component_key() below). Any registered component is
therefore writable from here. Field schemas per key are noted inline.

Usage: import this file, call the helpers to build entity dicts, append to a
list, then call write_scene(path, entities, scene_id, scene_name).

See also: memory note "wscene format cheat sheet" (project_wscene_cheatsheet.md)
for the narrative version of everything below.
"""
import json
import math

def _fnv1a64(text):
    h = 0xCBF29CE484222325
    for b in text.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h

def component_key(component_name):
    """fnv1a64 of a component's COMPONENT_NAME (see Game::TypeSID). Full 64 bits, unlike name_id().
    If one stops matching, that component's COMPONENT_NAME changed and every asset needs migrating."""
    return str(_fnv1a64(component_name))

# ---- component-type keys ----
TRANSFORM = component_key("TransformComponent")             # translation/rotation(wxyz)/scale
NAME = component_key("NameComponent")                       # name
STABLEID = component_key("StableIdComponent")               # id, sortOrder
FOLDER = component_key("EntityFolderComponent")             # folderId (0 = root/no folder, always safe)
HIERARCHY = component_key("HierarchyComponent")             # parentStableId (StringID of parent's StableIdComponent.id)
SCENE_FOLDER = component_key("SceneFolderComponent")        # folder metadata pseudo-entities: folderId, name, parentFolder
PROCEDURAL = component_key("ProceduralMeshComponent")       # flattened shape params + material/modelFlags/renderOffset/renderRotation/type
PHYSICS = component_key("PhysicsBodyDesc")                  # motionType/mass/friction/restitution/motionQuality/layerOverride/.../shapes[]
TEXT3D = component_key("Text3DComponent")                   # fontId/text/depth/flatness/tracking/scale/smoothNormals/material/modelFlags/renderOffset/renderRotation
SPLINE = component_key("SplineMeshComponent")               # profile/railing/spline fields, flattened (see spline_fields())
STATIC_MESH = component_key("StaticMeshComponent")          # modelId, modelFlags, materialOverrides{slot:id}, primitiveBlacklist[], renderOffset, renderRotation. ONE entity = one whole model.
STATIC_MESH_PRIMITIVE = component_key("StaticMeshPrimitiveComponent")  # modelId, primitiveOrdinal, modelFlags, renderOffset, renderRotation
SPAWN = component_key("PlayerSpawnComponent")               # offset, priority
LIGHT_DIRECTIONAL = component_key("DirectionalLightComponent")  # color, intensity, priority, angularRadiusDegrees; direction = rotation*(0,0,1), highest priority wins
LIGHT_AREA = component_key("AreaLightComponent")            # color[3], intensity, halfWidth, halfHeight, range, drawEmissiveSurface; world extent = half*transform.scale, emissive quad = unit XZ plane
LIGHT_SPHERE = component_key("SphereLightComponent")        # color[3], intensity, radius, range, drawEmissiveSurface; world radius = radius*transform.scale.x
SKYBOX = component_key("SkyboxComponent")                   # envMap (uint64 env map asset id), intensity, priority; highest priority wins, intensity MULTIPLIES profile iblIntensity
# NOTE: light `color` is packed to 8-bit [0,1] on the GPU -- HDR brightness MUST come from `intensity`, never color>1.
# `range` is the influence/falloff+cull radius (NOT the emissive size). `drawEmissiveSurface`=visible glowing rep mesh.
GIZMO = component_key("DebugGizmoComponent")                # color, extents, lineWidth, shape (0=Box?,2=Sphere confirmed)
PROBE = component_key("ReflectionProbeComponent")           # probeId, bEnabled, shape, fadeMargin, captureOffset, bParallax, resolution, standInEnvMap

# Schemas below observed in level0.wscene; payload fields verified there, not against source.
PREFAB_INSTANCE = component_key("PrefabInstanceComponent")  # prefabId (matches a .wprefab header id), bMasterPrefab
CHECKPOINT = component_key("CheckpointComponent")           # checkpointId, priority, spawnOffset[3], spawnRotation[3]
PATH_MOVER = component_key("PathMoverComponent")            # pointSettings[]{easing,speed,waitTime,rotation[xyzw]}, loopMode, direction, currentSegment, bIsWaiting
DEATH_ZONE = component_key("DeathZoneComponent")            # tag, payload is null
WORLD_TEXT = component_key("TextComponent")                 # text, fontId, textMaterialId, renderSizePx, color[4]

# Registered but unused by any authored scene so far; keys are still correct, schemas are not
# documented here: FreeCameraComponent, CharacterPhysicsComponent, DrawPhysicsDebugTag,
# MotionBlurMovementComponent, AntiGravityTag, FloorTag, RotateInPlaceComponent.

# =============================================================================
# .wscene file structure
# =============================================================================
# Text header, then a JSON body:
#   wscene
#   version 1 0
#   id <uint64 scene id>
#   name <scene name>
#   entity_count <n>
#   end_header
#   { "editor_camera": {"rotation":[w,x,y,z]  (SAME as entities -- scene_system.cpp:233/320),
#                        "translation":[x,y,z]},
#     "entities": [ {<entity object>}, ... ],
#     "scene_id": <same uint64 as header id>,
#     "scene_name": <same as header name> }
#
# Each entity is a JSON object keyed by the decimal-string component keys above.
# Quaternions on TransformComponent/shape rotations are stored [w,x,y,z].

# =============================================================================
# Procedural shape params -- variant index ("type" on the render component,
# "proceduralType" on the physics shape) + required field names, confirmed
# against physics_body_desc.cpp Serialize()/Deserialize() and template scene
# instances. Pivot conventions (from src/engine/resources/physics/collider_generation.cpp
# comments, "mirrors Generate*") noted per shape -- IMPORTANT when positioning:
#
#   Box, Wedge, Staircase        : CORNER pivot, (0,0,0)..(size), local Y up
#   Plane                         : centered XZ, y=0
#   Cone                          : base at y=0, apex at y=height
#   Cylinder                      : CENTERED on Y (y in [-h/2, h/2])
#   SpiralStaircase               : CENTER pivot, helix around local Y axis (see spiral_math() below)
#   Pipe                          : centered Y
#   Torus                         : main ring in local XY plane (i.e. stands up like a wheel
#                                   facing +Z by default); lowest point ~ -(ringRadius+tubeRadius)
#   Arch, Door                    : assumed base-at-y=0, XZ centered (matches gate-like usage
#                                   in template scene; NOT explicitly commented in source)
#   Capsule, Sphere, SubdividedSphere, Hemisphere, Tetrahedron, Octahedron,
#   Icosahedron, Dodecahedron, KleinBottle, TrefoilKnot, Bowl, CurvedRamp
#                                  : NOT explicitly commented in collider_generation.cpp.
#                                    Best-guess assumed CENTERED at origin (sphere-like) or
#                                    base-at-0 (dome-like, Hemisphere) -- verify before relying
#                                    on exact placement for anything load-bearing/on-path.
# =============================================================================

def box_params(sx, sy, sz): return {"sizeX": sx, "sizeY": sy, "sizeZ": sz}, 2
def cylinder_params(radius, height, slices=16, capped=True): return {"radius": radius, "height": height, "slices": slices, "bCapped": capped}, 3
def capsule_params(radius, height, slices=16, rings=8): return {"radius": radius, "height": height, "slices": slices, "rings": rings}, 4
def torus_params(ring, tube, slices=32, stacks=32): return {"ringRadius": ring, "tubeRadius": tube, "slices": slices, "stacks": stacks}, 5
def arch_params(width, height, depth, thickness, sides=64, fill=True): return {"width": width, "height": height, "depth": depth, "thickness": thickness, "sides": sides, "bFillCorners": fill}, 6
def wedge_params(sx, sy, sz): return {"sizeX": sx, "sizeY": sy, "sizeZ": sz}, 7
def cone_params(radius, height, slices=16, capped=True): return {"radius": radius, "height": height, "slices": slices, "bCapped": capped}, 8
def door_params(width, height, depth, archHeight=0.5, gap=0.0, sides=8, half=False, flip=False): return {"width": width, "height": height, "depth": depth, "archHeight": archHeight, "gap": gap, "sides": sides, "bHalf": half, "bFlip": flip}, 9
def plane_params(sx, sz, tx=1, tz=1): return {"sizeX": sx, "sizeZ": sz, "tilesX": tx, "tilesZ": tz}, 10
def sphere_params(radius, slices=16, stacks=16): return {"radius": radius, "slices": slices, "stacks": stacks}, 11
def subsphere_params(radius, subdiv=3): return {"radius": radius, "subdivisions": subdiv}, 12
def hemisphere_params(radius, slices=16, stacks=8): return {"radius": radius, "slices": slices, "stacks": stacks}, 13
def pipe_params(outer, inner, height, slices=16): return {"outerRadius": outer, "innerRadius": inner, "height": height, "slices": slices}, 14
def tetra_params(radius): return {"radius": radius}, 15
def octa_params(radius): return {"radius": radius}, 16
def icosa_params(radius): return {"radius": radius}, 17
def dodeca_params(radius): return {"radius": radius}, 18
def klein_params(scale, slices=8, stacks=8): return {"scale": scale, "slices": slices, "stacks": stacks}, 19
def trefoil_params(scale, tube, slices=16, stacks=128): return {"scale": scale, "tubeRadius": tube, "slices": slices, "stacks": stacks}, 20
def curvedramp_params(width, height, radius, segments=16, halfpipe=False, flat=1.0, lip=0.2): return {"width": width, "height": height, "radius": radius, "segments": segments, "bHalfPipe": halfpipe, "flatLength": flat, "lipHeight": lip}, 21
def bowl_params(radius, height, curveRadius, flatRadius=0.0, segments=8, slices=16, lip=0.02): return {"radius": radius, "height": height, "curveRadius": curveRadius, "flatRadius": flatRadius, "segments": segments, "slices": slices, "lipHeight": lip}, 22
def staircase_params(stepCount, width, totalDepth, totalHeight, specifyStepHeight=False, stepHeight=0.2, closed=True):
    return {"stepCount": stepCount, "width": width, "totalDepth": totalDepth, "totalHeight": totalHeight, "bSpecifyStepHeight": specifyStepHeight, "stepHeight": stepHeight, "bIsClosed": closed}, 1
def spiral_params(stepCount, totalHeight, outerRadius, centerColumnRadius, treadThickness, totalSweep, arcSegments, showColumn, ramp, stepHeight=0.2, specifyStepHeight=False, degreesPerStep=30.0, specifyDegrees=False):
    return {"stepCount": stepCount, "stepHeight": stepHeight, "totalHeight": totalHeight, "bSpecifyStepHeight": specifyStepHeight,
            "outerRadius": outerRadius, "centerColumnRadius": centerColumnRadius, "treadThickness": treadThickness,
            "degreesPerStep": degreesPerStep, "totalSweep": totalSweep, "bSpecifyDegreesPerStep": specifyDegrees,
            "arcSegments": arcSegments, "bShowCenterColumn": showColumn, "bRamp": ramp}, 23
def ring_params(outer, inner, slices=32, doubleSided=True): return {"outerRadius": outer, "innerRadius": inner, "slices": slices, "bDoubleSided": doubleSided}, 24
def wall_params(size_x, size_y, size_z, openings=()):
    """Slab with up to 8 rectangular holes through its thickness (Z). Corner pivot like box_params.
    openings = iterable of (x, y, w, h) in face-plane coords: x along size_x, y along size_y, from the
    corner origin. One welded mesh, no coplanar overlaps; collider = compound of the solid spans.
    Replaces the three-boxes-plus-lintel doorway idiom."""
    ops = [list(o) for o in openings]
    if len(ops) > 8:
        raise ValueError(f"wall_params: {len(ops)} openings, max is 8")
    return {"sizeX": size_x, "sizeY": size_y, "sizeZ": size_z, "openings": ops}, 25

# proceduralType index -> variant name, for reference / error messages
PROC_TYPE_NAMES = ["monostate", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door",
                    "Plane", "Sphere", "SubdividedSphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
                    "Icosahedron", "Dodecahedron", "KleinBottle", "TrefoilKnot", "CurvedRamp", "Bowl", "SpiralStaircase", "Ring", "Wall"]

# =============================================================================
# Spiral staircase ramp-mode geometry (ported from CompoundSpiralStaircase in
# collider_generation.cpp) -- lets you compute the exact world-space entry
# (top) / exit (bottom) points of a bRamp=True spiral so connecting platforms
# line up without guesswork.
# =============================================================================
def spiral_math(outer_radius, total_height, total_sweep_deg, sample_radius=None):
    """Returns (local_point_at_angle_fn, entry_local, exit_local). Identity-rotation
    spiral: local (x,y,z) == world offset from the entity's translation. Ball
    travels TOP (angle=sweep, y=total_height) -> BOTTOM (angle=0, y=0).

    sample_radius controls where on the tread the entry/exit anchor point sits --
    default is outer_radius, but the tread is an ANNULUS from centerColumnRadius to
    outer_radius (~outer_radius-centerColumnRadius wide), so anchoring connecting
    platforms to the outer edge leaves most of the tread uncovered on one side and
    overhangs empty space on the other. Pass sample_radius=(center_column_radius +
    outer_radius) / 2 to anchor at the middle of the tread instead (bit us once
    already: 2026-07-04, see project_ballance_level.md)."""
    r = outer_radius if sample_radius is None else sample_radius
    sweep_rad = math.radians(total_sweep_deg)
    def local_at(angle_rad):
        h = (angle_rad / sweep_rad) * total_height
        return (math.cos(angle_rad) * r, h, math.sin(angle_rad) * r)
    return local_at, local_at(sweep_rad), local_at(0.0)

# =============================================================================
# id / entity assembly
# =============================================================================
def name_id(name):
    """FNV-1a 64 of `name`, masked to 63 bits. Order-independent, so unlike next_id() it survives
    entities/assets being added or removed ahead of it. Use it for every id an asset is keyed by
    (material ids, probeIds, scene ids): the id then follows the unique asset NAME and two
    generators can never mint the same one."""
    h = 0xCBF29CE484222325
    for b in name.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h & 0x7FFFFFFFFFFFFFFF

_ctr = [0x1234567890ABCDEF]

def seed_ids(namespace):
    """Reseed next_id() from `namespace`. CALL THIS FIRST in every generator script, with the
    scene's name. next_id() is a fixed deterministic sequence, so two scripts that both start
    from the default seed hand out THE SAME ids -- which silently collided the lighting lab's
    first 7 materials with the probe orientation room's 7 emissives (whichever asset scanned
    last won the id, so the lab rendered with emissive walls). Anything an asset is keyed by
    should come from name_id(), not from this sequence at all."""
    _ctr[0] = name_id(namespace) | 1

def next_id():
    """splitmix64-style unique id -- unique only WITHIN one seed_ids() namespace, and only for
    a fixed call order. Fine for entity stable ids; do NOT key assets off it (see seed_ids)."""
    _ctr[0] = (_ctr[0] + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = _ctr[0]
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    z = z ^ (z >> 31)
    return z & 0x7FFFFFFFFFFFFFFF

_sort = [0]
def next_sort():
    _sort[0] += 1
    return _sort[0]

RENDER_DEFAULTS = {"material": 0, "modelFlags": [1.0, 1.0, 0.0, 0.0], "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0]}

def base_entity(name, pos, rot=(1.0, 0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), folder_id=0):
    return {
        TRANSFORM: {"translation": list(pos), "rotation": list(rot), "scale": list(scale)},
        NAME: {"name": name},
        STABLEID: {"id": next_id(), "sortOrder": next_sort()},
        FOLDER: {"folderId": folder_id},
    }

def add_procedural(entity, ptype_idx, fields, motion=0, friction=0.5, restitution=0.0):
    """motion: 0=Static, 1=Kinematic, 2=Dynamic (Game::Component::PhysicsMotionType order)."""
    entity[PROCEDURAL] = {**fields, **RENDER_DEFAULTS, "type": ptype_idx}
    shape = {
        **fields, "type": 3,  # PhysicsShapeType::Collider (the only mesh-backed shape type post Stage-4b)
        "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0],
        "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0,
        "meshSourceModelId": 0, "proceduralType": ptype_idx,
    }
    entity[PHYSICS] = {
        "motionType": motion, "mass": 1.0, "friction": friction, "restitution": restitution,
        "motionQuality": 0, "layerOverride": 65535,
        "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape],
    }
    return entity

# ---- lights (no physics; a light entity is just Transform + light component) ----
# All keys/fields verified 2026-07-21 against light_components.cpp + isolated_*_light.wscene.
# HDR brightness comes from `intensity`; `color` is clamped to 8-bit [0,1] on upload.
def add_area_light(entity, color=(1.0, 1.0, 1.0), intensity=100.0, half_width=1.0, half_height=1.0, draw_range=100.0, draw_emissive=True):
    """Rectangular area light. World extent = (half_width,half_height)*transform.scale; emissive quad lies in the local XZ plane.
    `draw_range` is the influence/falloff+cull radius, NOT the emissive size."""
    entity[LIGHT_AREA] = {"color": list(color), "intensity": intensity, "halfWidth": half_width,
                           "halfHeight": half_height, "range": draw_range, "drawEmissiveSurface": draw_emissive}
    return entity

def add_sphere_light(entity, color=(1.0, 1.0, 1.0), intensity=100.0, radius=0.5, draw_range=100.0, draw_emissive=True):
    """Sphere (point-like) light. World radius = radius*transform.scale.x. Use radius~0.05 for a near-point emitter."""
    entity[LIGHT_SPHERE] = {"color": list(color), "intensity": intensity, "radius": radius,
                             "range": draw_range, "drawEmissiveSurface": draw_emissive}
    return entity

def add_directional_light(entity, color=(1.0, 1.0, 1.0), intensity=2.0, priority=0, angular_radius_deg=1.0):
    """Sun. Direction = transform.rotation * (0,0,1) (local +Z). Highest `priority` wins when several exist.
    angular_radius_deg = sun-disk half-angle (0 = hard shadow, larger = softer penumbra)."""
    entity[LIGHT_DIRECTIONAL] = {"color": list(color), "intensity": intensity, "priority": priority,
                                  "angularRadiusDegrees": angular_radius_deg}
    return entity

def add_skybox(entity, envmap_id, intensity=1.0, priority=0):
    """Scene-declared sky. envmap_id = env map asset id (asset_index.envmap(name)). Highest priority
    wins; while active it drives the skybox and its intensity MULTIPLIES the lighting profile's
    iblIntensity (background pass included). Transform is ignored."""
    entity[SKYBOX] = {"envMap": envmap_id, "intensity": intensity, "priority": priority}
    return entity

# ---- reflection probes (Transform + probe component; no physics, no mesh) ----
# Verified 2026-07-27 against reflection_probe_component.cpp Serialize() + render_systems.cpp:1664.
PROBE_BOX, PROBE_SPHERE = 0, 1
PROBE_RES_128, PROBE_RES_256 = 0, 1

def add_reflection_probe(entity, probe_id, shape=PROBE_BOX, fade_margin=0.0, capture_offset=(0.0, 0.0, 0.0),
                         parallax=True, resolution=PROBE_RES_256, stand_in_env_map=0, enabled=True):
    """Box probe bounds = transform.scale as HALF-EXTENTS (sphere: max component = radius), rotated by
    transform.rotation. The capture is taken at translation + rotation*capture_offset -- keep that point
    out of any mesh or emissive light rep, or the cubemap bakes the inside of that object.
    fade_margin is a world-space fade band at the boundary (0 = hard edge, right for a sealed room whose
    walls sit on the boundary). Shading picks the SMALLEST-volume probe containing the pixel, so a nested
    room can simply get its own smaller probe. `probe_id` must be non-zero and stable across runs --
    it names the baked assets/probes/probe_<id>.wprobe; use name_id("..."). stand_in_env_map 0 = none
    (probe contributes nothing until baked)."""
    entity[PROBE] = {"probeId": probe_id, "bEnabled": enabled, "shape": shape, "fadeMargin": fade_margin,
                      "captureOffset": list(capture_offset), "bParallax": parallax,
                      "resolution": resolution, "standInEnvMap": stand_in_env_map}
    return entity

ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT = 0, 1, 2
ANCHOR_BASELINE, ANCHOR_TOP, ANCHOR_CENTER, ANCHOR_BOTTOM = 0, 1, 2, 3

def add_text3d(entity, text, font_id, depth=0.2, flatness=0.0005, tracking=0.05, scale=1.0, smooth=True, precise=False, motion=0,
               align=ALIGN_LEFT, anchor=ANCHOR_BASELINE):
    """Physics shape for Text3D is a box-per-glyph Compound unless precise=True (concave
    TriangleMesh, Static/Kinematic only). font_id must come from an existing scene/asset.
    text breaks lines on '\\n'; align/anchor place the block relative to the entity origin."""
    entity[TEXT3D] = {"depth": depth, "flatness": flatness, "fontId": font_id, **RENDER_DEFAULTS,
                       "scale": scale, "smoothNormals": smooth, "text": text, "tracking": tracking,
                       "align": align, "anchor": anchor}
    shape = {"type": 3, "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0],
             "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0, "meshSourceModelId": 0, "proceduralType": 0,
             "text3DSource": {"fontId": font_id, "text": text, "depth": depth, "flatness": flatness,
                               "tracking": tracking, "scale": scale, "smoothNormals": smooth, "precise": precise,
                               "align": align, "anchor": anchor}}
    entity[PHYSICS] = {"motionType": motion, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                        "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape]}
    return entity

def spline_fields(spline_points, closed=False, mode=1, radius=0.5, roll_angle=0.0, sides=8, segments_per_span=8,
                   caps=True, cross_planks=False, profile_type=0, profile_w=0.4, profile_h=0.4, profile_corner_r=0.08,
                   profile_corner_seg=3, profile_thickness=0.05, railing_enabled=False, railing_lanes=None,
                   railing_posts=True, railing_post_interval=4, railing_post_bottom=0.0, railing_post_top=1.0,
                   railing_post_size=(0.05, 0.05), railing_post_lateral=0.0, railing_lateral_offset=0.0,
                   cross_plank_interval=4, cross_plank_height=0.0, cross_plank_thickness=0.1, cross_plank_length=0.3):
    """Flattened field set for both SplineMeshComponent (render) and PhysicsBodyDesc shape's
    nested "splineParams" (see how these two are combined in a full spline entity example in
    the template scene -- SPLINE render key gets these fields directly; PHYSICS shape gets them
    nested under "splineParams"). mode: 0=Linear, 1=CatmullRom (Engine::SplineMode). Spline.MaxPoints=64."""
    if railing_lanes is None:
        railing_lanes = [[0.0, 0.0]]
    return {
        "bCaps": caps, "bCrossPlanks": cross_planks, "crossPlankInterval": cross_plank_interval,
        "crossPlankHeight": cross_plank_height, "crossPlankThickness": cross_plank_thickness, "crossPlankLength": cross_plank_length,
        "profileCornerRadius": profile_corner_r, "profileCornerSegments": profile_corner_seg, "profileHeight": profile_h,
        "profileThickness": profile_thickness, "profileType": profile_type, "profileWidth": profile_w,
        "radius": radius, "railingEnabled": railing_enabled, "railingLanes": railing_lanes,
        "railingLateralOffset": railing_lateral_offset, "railingPostBottom": railing_post_bottom,
        "railingPostInterval": railing_post_interval, "railingPostLateral": railing_post_lateral,
        "railingPostSizeX": railing_post_size[0], "railingPostSizeY": railing_post_size[1],
        "railingPostTop": railing_post_top, "railingPosts": railing_posts, "rollAngle": roll_angle,
        "segmentsPerSpan": segments_per_span, "sides": sides,
        "spline": {"bClosed": closed, "mode": mode, "points": [list(p) for p in spline_points]},
    }

# =============================================================================
# oriented-box math (corner-pivot aware) -- for ramps/platforms/walls between
# two 3D waypoints without hand-deriving quaternions.
# =============================================================================
def _sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def _add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def _scale(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def _dot(a, b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def _cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def _length(a): return math.sqrt(_dot(a, a))
def _normalize(a):
    l = _length(a)
    return (a[0]/l, a[1]/l, a[2]/l)

def mat_to_quat(right, up, fwd):
    """3x3 rotation matrix (columns = local X,Y,Z expressed in world space) -> [w,x,y,z]."""
    m00, m10, m20 = right
    m01, m11, m21 = up
    m02, m12, m22 = fwd
    trace = m00 + m11 + m22
    if trace > 0:
        s = 0.5 / math.sqrt(trace + 1.0)
        w, x, y, z = 0.25/s, (m21-m12)*s, (m02-m20)*s, (m10-m01)*s
    elif m00 > m11 and m00 > m22:
        s = 2.0 * math.sqrt(1.0+m00-m11-m22)
        w, x, y, z = (m21-m12)/s, 0.25*s, (m01+m10)/s, (m02+m20)/s
    elif m11 > m22:
        s = 2.0 * math.sqrt(1.0+m11-m00-m22)
        w, x, y, z = (m02-m20)/s, (m01+m10)/s, 0.25*s, (m12+m21)/s
    else:
        s = 2.0 * math.sqrt(1.0+m22-m00-m11)
        w, x, y, z = (m10-m01)/s, (m02+m20)/s, (m12+m21)/s, 0.25*s
    n = math.sqrt(w*w+x*x+y*y+z*z)
    return (w/n, x/n, y/n, z/n)

def basis_for(p_start, p_end, world_up=(0.0, 1.0, 0.0)):
    """right/up/fwd MUST satisfy cross(right,up) == fwd (a proper rotation, det=+1) --
    glm::mat3_cast(quat) columns are (X,Y,Z) and for any valid quaternion cross(col0,col1)==col2
    (confirmed against src/game/systems/render_systems.cpp:1194-1197 and light_components.cpp:186-189,
    which read rot[0]/rot[1]/rot[2] as right/up/normal off mat3_cast). Getting the cross-product
    argument order backwards here produces a REFLECTION (det=-1), which mat_to_quat has no way to
    represent correctly -- it silently returns some other rotation, so the box ends up in the wrong
    place AND mirrored. (This bit us once already: 2026-07-04, see project_ballance_level.md.)
    """
    d = _sub(p_end, p_start)
    fwd = _normalize(d)
    if abs(_dot(fwd, world_up)) > 0.999:
        world_up = (1.0, 0.0, 0.0)
    right = _normalize(_cross(world_up, fwd))
    up = _normalize(_cross(fwd, right))
    check = _cross(right, up)
    assert _dot(check, fwd) > 0.999, "basis_for produced a reflection, not a rotation -- fix the cross() argument order above"
    return right, up, fwd, _length(d)

def oriented_box(p_start, p_end, width, thickness, lateral_offset=0.0, vertical_offset=0.0, world_up=(0.0, 1.0, 0.0)):
    """Returns (corner_position, rotation_wxyz, length) for a corner-pivot Box (GenerateBox
    convention: local (0,0,0)..(sizeX,sizeY,sizeZ), Y up) whose TOP FACE CENTER runs along the
    line p_start->p_end, offset laterally (+right) / vertically (+up)."""
    right, up, fwd, length_ = basis_for(p_start, p_end, world_up)
    mid = _scale(_add(p_start, p_end), 0.5)
    target_top_center = _add(_add(mid, _scale(right, lateral_offset)), _scale(up, vertical_offset))
    half_local = (width * 0.5, thickness, length_ * 0.5)
    offset_world = _add(_add(_scale(right, half_local[0]), _scale(up, half_local[1])), _scale(fwd, half_local[2]))
    corner = _sub(target_top_center, offset_world)
    quat = mat_to_quat(right, up, fwd)
    return corner, quat, length_

def add_platform(entities, name, p_start, p_end, width, thickness=0.4, motion=0, friction=0.5):
    corner, quat, length_ = oriented_box(p_start, p_end, width, thickness)
    e = base_entity(name, corner, quat)
    fields, idx = box_params(width, thickness, length_)
    add_procedural(e, idx, fields, motion=motion, friction=friction)
    entities.append(e)
    return e

def add_wall(entities, name, p_start, p_end, lateral_offset, height=0.6, thickness=0.15, embed=0.1):
    corner, quat, length_ = oriented_box(p_start, p_end, thickness, height, lateral_offset=lateral_offset, vertical_offset=(height - embed))
    e = base_entity(name, corner, quat)
    fields, idx = box_params(thickness, height, length_)
    add_procedural(e, idx, fields, motion=0)
    entities.append(e)
    return e

# =============================================================================
# gameplay / structural components
# Field names verified 2026-07-29 against each component's Serialize().
# =============================================================================
def add_prefab_instance(entity, prefab_id, master=False):
    """Marks the entity as an instance of assets/prefabs/<x>.wprefab (prefab_id = that file's
    header id; asset_index.prefab("Barrel")). The entity still needs its own components -- this
    records provenance so editor edits can propagate, it does not expand the prefab for you."""
    entity[PREFAB_INSTANCE] = {"prefabId": prefab_id, "bMasterPrefab": master}
    return entity

def set_parent(child, parent):
    """Parents child to parent by stable id. IMPORTANT: a parented entity's TransformComponent
    becomes LOCAL to the parent (core_components.h:28), so translation/rotation/scale you already
    set are reinterpreted as offsets -- parent first, then position, or compose it yourself.
    Physics stays world-authoritative regardless."""
    entity_id = parent[STABLEID]["id"]
    child[HIERARCHY] = {"parentStableId": entity_id}
    return child

def add_checkpoint(entity, checkpoint_id, priority=0, spawn_offset=(0.0, 0.0, 0.0), spawn_rotation=(0.0, 0.0, 0.0)):
    """Respawn point. spawn_rotation is EULER xyz here, not a quaternion (unlike TransformComponent).
    Highest `priority` wins when several are active. checkpoint_id must be stable across runs: name_id("...")."""
    entity[CHECKPOINT] = {"checkpointId": checkpoint_id, "priority": priority,
                          "spawnOffset": list(spawn_offset), "spawnRotation": list(spawn_rotation)}
    return entity

def add_world_text(entity, text, font_id, text_material_id=0, render_size_px=48.0, color=(1.0, 1.0, 1.0, 1.0)):
    """Screen-facing text billboarded in the world. Distinct from add_text3d(), which extrudes real
    geometry and carries a collider; this one is a camera-facing glyph quad with no physics."""
    entity[WORLD_TEXT] = {"text": text, "fontId": font_id, "textMaterialId": text_material_id,
                          "renderSizePx": render_size_px, "color": list(color)}
    return entity

# =============================================================================
# kit layout -- placement maths only, so the caller picks the material/motion per piece
# =============================================================================
ROOM_FACES = ("floor", "ceiling", "west", "east", "south", "north")

def room_boxes(inner_size, wall_t=0.25, origin=(0.0, 0.0, 0.0), faces=ROOM_FACES):
    """Slabs for a box room whose INTERIOR is exactly inner_size starting at origin.
    Returns [(face, corner_pos, size)] ready for box_params, which is corner-pivot.
    Faces tile without overlapping: floor/ceiling take the full footprint, west/east span
    the full Z, south/north fill only the remaining X gap. Omit faces for an open room."""
    ox, oy, oz = origin
    sx, sy, sz = inner_size
    t = wall_t
    out = []
    for face in faces:
        if face == "floor":
            out.append((face, (ox - t, oy - t, oz - t), (sx + 2 * t, t, sz + 2 * t)))
        elif face == "ceiling":
            out.append((face, (ox - t, oy + sy, oz - t), (sx + 2 * t, t, sz + 2 * t)))
        elif face == "west":
            out.append((face, (ox - t, oy, oz - t), (t, sy, sz + 2 * t)))
        elif face == "east":
            out.append((face, (ox + sx, oy, oz - t), (t, sy, sz + 2 * t)))
        elif face == "south":
            out.append((face, (ox, oy, oz - t), (sx, sy, t)))
        elif face == "north":
            out.append((face, (ox, oy, oz + sz), (sx, sy, t)))
        else:
            raise ValueError("unknown room face {!r}, expected one of {}".format(face, ROOM_FACES))
    return out

def add_room(entities, name_prefix, inner_size, wall_t=0.25, origin=(0.0, 0.0, 0.0), faces=ROOM_FACES,
             material=0, motion=0, folder_id=0):
    """Builds room_boxes() as entities named "<prefix> <face>". Returns them in face order."""
    made = []
    for face, pos, size in room_boxes(inner_size, wall_t, origin, faces):
        e = base_entity("{} {}".format(name_prefix, face), pos, folder_id=folder_id)
        fields, idx = box_params(*size)
        add_procedural(e, idx, fields, motion=motion)
        e[PROCEDURAL]["material"] = material
        entities.append(e)
        made.append(e)
    return made

def grid_positions(cols, rows, spacing, origin=(0.0, 0.0, 0.0)):
    """[(pos, col, row)] on the XZ plane. spacing is (dx, dz) or a scalar."""
    dx, dz = spacing if isinstance(spacing, (tuple, list)) else (spacing, spacing)
    ox, oy, oz = origin
    return [((ox + c * dx, oy, oz + r * dz), c, r) for r in range(rows) for c in range(cols)]

def floor_levels(count, storey_height, origin_y=0.0):
    """Y offsets for stacked storeys. storey_height must already include the slab thickness,
    or successive floors interpenetrate: interior height + wall_t is the usual value."""
    return [origin_y + i * storey_height for i in range(count)]

# =============================================================================
# top-level write
# =============================================================================
def write_scene(path, entities, scene_id, scene_name, editor_camera=None):
    if editor_camera is None:
        # editor_camera rotation is [w,x,y,z] -- SAME as entity quats, NOT [x,y,z,w].
        # (scene_system.cpp:233 writes {w,x,y,z}; :320 reads glm::quat(w,x,y,z).)
        # [1,0,0,0] = identity/upright. A stray w=0 here flips the camera upside-down.
        editor_camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 4.0, 12.0]}
    body = {"editor_camera": editor_camera, "entities": entities, "scene_id": scene_id, "scene_name": scene_name}
    header = f"wscene\nversion 1 0\nid {scene_id}\nname {scene_name}\nentity_count {len(entities)}\nend_header\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(header)
        f.write(json.dumps(body, indent=2))
    # sanity re-parse
    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    hdr_end = next(i for i, l in enumerate(lines) if l.strip() == "end_header")
    reparsed = json.loads("".join(lines[hdr_end + 1:]))
    assert len(reparsed["entities"]) == len(entities)
    return path


def read_scene(path):
    """Parse an existing .wscene into (header_dict, body_dict) for inspection/reuse
    (e.g. copying a fontId, checking what component keys a scene actually uses)."""
    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    hdr_end = next(i for i, l in enumerate(lines) if l.strip() == "end_header")
    header_lines = [l.strip() for l in lines[:hdr_end]]
    body = json.loads("".join(lines[hdr_end + 1:]))
    return header_lines, body
