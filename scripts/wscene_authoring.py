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
import os
import re

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
MODULE = component_key("ModuleMeshComponent")               # parts[]{type, <shape fields>, offset[3], rotation[wxyz], slot}, slotMaterials[8], modelFlags; see add_module()
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
LOCAL_DDGI = component_key("LocalDDGIVolumeComponent")      # volumeId, bEnabled, probeSpacing; bounds = transform.scale half-extents, rotation ignored

# Schemas below observed in level0.wscene; payload fields verified there, not against source.
PREFAB_INSTANCE = component_key("PrefabInstanceComponent")  # prefabId (matches a .wprefab header id), bMasterPrefab
CHECKPOINT = component_key("CheckpointComponent")           # checkpointId, priority, spawnOffset[3], spawnRotation[3]
PATH_MOVER = component_key("PathMoverComponent")            # spline{bClosed,mode,points}, pointSettings[]{easing,speed,waitTime,rotation[xyzw] NOT wxyz}, loopMode, + runtime state; see add_path_mover()
DEATH_ZONE = component_key("DeathZoneComponent")            # tag, payload is null
WORLD_TEXT = component_key("TextComponent")                 # text, fontId, textMaterialId, renderSizePx, color[4], align, anchor, wrapWidthPx

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

def lattice_params(size_x, size_y, size_z, chord=0.06, brace=0.04, bays=4, pattern=0):
    """4-chord truss in the corner-pivot envelope, long axis Y. Chords at the XZ footprint corners,
    a horizontal brace ring at every bay boundary. pattern: 0 = X-brace (both diagonals per side face
    per bay), 1 = single diagonal alternating per bay. Collider = compound box per member.
    Replaces hand-placed mast brace/diagonal entity stacks."""
    return {"sizeX": size_x, "sizeY": size_y, "sizeZ": size_z, "chordSize": chord, "braceSize": brace,
            "bayCount": bays, "pattern": pattern}, 26

def corrugated_params(size_x, size_y, size_z=0.05, rib_depth=0.05, rib_width=0.2, rib_count=6):
    """Corrugated sheet: flat back at z=0, web size_z thick, rib_count trapezoidal ribs protruding to
    z = size_z + rib_depth, evenly pitched across X, running the full Y (45-degree flanks, steepened
    when the pitch cannot fit them). Corner pivot. Collider = web box + one box per rib.
    Replaces per-rib box stacks on container-style panels."""
    return {"sizeX": size_x, "sizeY": size_y, "sizeZ": size_z, "ribDepth": rib_depth,
            "ribWidth": rib_width, "ribCount": rib_count}, 27

# proceduralType index -> variant name, for reference / error messages
PROC_TYPE_NAMES = ["monostate", "Staircase", "Box", "Cylinder", "Capsule", "Torus", "Arch", "Wedge", "Cone", "Door",
                    "Plane", "Sphere", "SubdividedSphere", "Hemisphere", "Pipe", "Tetrahedron", "Octahedron",
                    "Icosahedron", "Dodecahedron", "KleinBottle", "TrefoilKnot", "CurvedRamp", "Bowl", "SpiralStaircase", "Ring", "Wall",
                    "Lattice", "CorrugatedPanel"]

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
    fade_margin is a world-space fade band at the boundary (0 = hard edge). Size the box to the OUTER
    shell, not the inner faces: wall pixels exactly on the boundary flicker between probe and skybox
    under jitter (seen in gi_sunbounce). Shading picks the SMALLEST-volume probe containing the pixel, so a nested
    room can simply get its own smaller probe. `probe_id` must be non-zero and stable across runs --
    it names the baked assets/probes/probe_<id>.wprobe; use name_id("..."). stand_in_env_map 0 = none
    (probe contributes nothing until baked)."""
    entity[PROBE] = {"probeId": probe_id, "bEnabled": enabled, "shape": shape, "fadeMargin": fade_margin,
                      "captureOffset": list(capture_offset), "bParallax": parallax,
                      "resolution": resolution, "standInEnvMap": stand_in_env_map}
    return entity

def add_local_ddgi_volume(entity, volume_id, probe_count, probe_spacing=0.5, enabled=True):
    """Axis-aligned local DDGI probe window: entity translation = window MIN CORNER (engine snaps it
    to the nearest spacing multiple), probe_count = probes per axis (2..16), window span =
    (count-1)*spacing per axis. Rotation and scale IGNORED (world-axis lattice). Size the window
    PAST the walls by ~edgeBlendCells*spacing so the edge fade band lies outside the interior;
    exact-fit volumes hand wall pixels back to the leaky cascades.
    volume_id must be non-zero and stable across runs; use name_id("...")."""
    counts = [max(2, min(16, int(c))) for c in probe_count]
    entity[LOCAL_DDGI] = {"volumeId": volume_id, "bEnabled": enabled, "probeSpacing": probe_spacing,
                          "probeCount": counts}
    return entity

ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT = 0, 1, 2
ANCHOR_BASELINE, ANCHOR_TOP, ANCHOR_CENTER, ANCHOR_BOTTOM = 0, 1, 2, 3

def add_text3d(entity, text, font_id, depth=0.2, flatness=0.0005, tracking=0.05, scale=1.0, smooth=True, precise=False, motion=0,
               align=ALIGN_LEFT, anchor=ANCHOR_BASELINE, wrap_width=0.0, bend_radius=0.0):
    """Physics shape for Text3D is a box-per-glyph Compound unless precise=True (concave
    TriangleMesh, Static/Kinematic only). font_id must come from an existing scene/asset.
    text breaks lines on '\\n'; align/anchor place the block relative to the entity origin.
    wrap_width word-wraps at that width in world units (0 = off). bend_radius wraps the block
    around a vertical cylinder of that radius (positive bulges toward +Z, negative concave, 0 = off);
    align=ALIGN_CENTER keeps the bend symmetric about the entity origin."""
    entity[TEXT3D] = {"depth": depth, "flatness": flatness, "fontId": font_id, **RENDER_DEFAULTS,
                       "scale": scale, "smoothNormals": smooth, "text": text, "tracking": tracking,
                       "align": align, "anchor": anchor, "wrapWidth": wrap_width, "bendRadius": bend_radius}
    shape = {"type": 3, "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0],
             "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0, "meshSourceModelId": 0, "proceduralType": 0,
             "text3DSource": {"fontId": font_id, "text": text, "depth": depth, "flatness": flatness,
                               "tracking": tracking, "scale": scale, "smoothNormals": smooth, "precise": precise,
                               "align": align, "anchor": anchor, "wrapWidth": wrap_width, "bendRadius": bend_radius}}
    entity[PHYSICS] = {"motionType": motion, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                        "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape]}
    return entity

def module_part(shape, offset=(0.0, 0.0, 0.0), rotation=(1.0, 0.0, 0.0, 0.0), slot=0):
    """One part of a module: shape = the (fields, idx) pair any *_params helper returns.
    rotation is [w,x,y,z]. slot = material slot 0..7; parts sharing a slot merge into one primitive."""
    fields, idx = shape
    if not 0 <= slot <= 7:
        raise ValueError(f"module_part: slot {slot} out of range 0..7")
    return {"type": idx, **fields, "offset": list(offset), "rotation": list(rotation), "slot": slot}

def add_module(entity, parts, materials=(), model_flags=(1.0, 1.0, 0.0, 0.0)):
    """Kit piece: up to 32 module_part() shapes baked into ONE content-hashed model with one
    primitive per used material slot; identical part lists dedupe across entities. materials =
    material id per slot (0 or omitted = engine default material) -- re-skin per instance by
    changing this list only. Modules have NO collider asset: give the entity a PhysicsBodyDesc
    with a few per-part procedural collider shapes if it needs collision."""
    parts = list(parts)
    if len(parts) > 32:
        raise ValueError(f"add_module: {len(parts)} parts, max is 32")
    mats = [materials[i] if i < len(materials) else 0 for i in range(8)]
    entity[MODULE] = {"modelFlags": list(model_flags), "renderOffset": [0.0, 0.0, 0.0],
                      "renderRotation": [1.0, 0.0, 0.0, 0.0], "slotMaterials": mats, "parts": parts}
    return entity

def add_static_mesh(entity, model_id, lighting_shader=0, shading_shader=0, material_overrides=None, primitive_blacklist=None):
    """Whole imported model (.wsmesh) as ONE entity; model_id = asset_index.model(name).
    lighting_shader/shading_shader = _fnv1a64 of a pipeline name (e.g. "default_pbr_restir"); 0 keeps
    each imported material's own shader (imports default to default_pbr, NOT the restir one).
    material_overrides = {slot: material_id} (cap 32); primitive_blacklist = [ordinal, ...] (cap 64).
    No physics; add a PhysicsBodyDesc separately if the model needs collision."""
    comp = {"modelId": model_id, "modelFlags": [1.0, 1.0, 0.0, 0.0],
            "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0]}
    if lighting_shader:
        comp["lightingShaderOverride"] = lighting_shader
    if shading_shader:
        comp["shadingShaderOverride"] = shading_shader
    if material_overrides:
        comp["materialOverrides"] = {str(slot): mid for slot, mid in material_overrides.items()}
    if primitive_blacklist:
        comp["primitiveBlacklist"] = list(primitive_blacklist)
    entity[STATIC_MESH] = comp
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

LOOP_ONCE, LOOP_LOOP, LOOP_PINGPONG = 0, 1, 2
# EasingType order in path_mover_component.h (verified 2026-07-31): Linear, EaseInQuad, EaseOutQuad,
# EaseInOutQuad, EaseInCubic, EaseOutCubic, EaseInOutCubic, EaseInQuart, EaseOutQuart, EaseInOutQuart,
# EaseInExpo, EaseOutExpo, EaseInOutExpo, EaseInSine, EaseOutSine, EaseInOutSine
EASE_LINEAR, EASE_IN_OUT_QUAD, EASE_IN_OUT_CUBIC, EASE_IN_OUT_SINE = 0, 3, 6, 15

def add_path_mover(entity, points, speed=1.0, wait_time=0.0, easing=EASE_LINEAR, loop_mode=LOOP_PINGPONG,
                   mode=1, point_settings=None):
    """Moves the entity along a spline of world-space points (mode: 0=Linear, 1=CatmullRom;
    Spline.MaxPoints=64). Pair with a Kinematic physics body (motionType=1) so the platform sweeps
    riders instead of teleporting through them. speed/wait_time/easing broadcast to every point;
    point_settings (list of dicts, one per point) overrides per point with keys speed, waitTime,
    easing, rotation. IMPORTANT: pointSettings rotation is [x,y,z,w] (path_mover_component.cpp:100),
    UNLIKE TransformComponent quats which are [w,x,y,z]. loop_mode: 0=Once, 1=Loop (deserialize
    force-closes the spline), 2=PingPong."""
    pts = [list(p) for p in points]
    if len(pts) > 64:
        raise ValueError(f"add_path_mover: {len(pts)} points, Spline.MaxPoints is 64")
    settings = []
    for i in range(len(pts)):
        ps = {"rotation": [0.0, 0.0, 0.0, 1.0], "easing": easing, "speed": speed, "waitTime": wait_time}
        if point_settings is not None and i < len(point_settings):
            ps.update(point_settings[i])
        settings.append(ps)
    entity[PATH_MOVER] = {
        "loopMode": loop_mode,
        "spline": {"bClosed": loop_mode == LOOP_LOOP, "mode": mode, "points": pts},
        "pointSettings": settings,
        "currentSegment": 0, "progress": 0.0, "direction": 1, "bIsWaiting": False, "waitTimer": 0.0,
    }
    return entity

def add_world_text(entity, text, font_id, text_material_id=0, render_size_px=48.0, color=(1.0, 1.0, 1.0, 1.0),
                   align=ALIGN_LEFT, anchor=ANCHOR_BASELINE, wrap_width_px=0.0):
    """Flat MSDF text in the entity's local XY plane (oriented by the entity transform, NOT billboarded).
    Distinct from add_text3d(), which extrudes real geometry and carries a collider; this one has no physics.
    Quad coordinates are in px (render_size_px tall per line); wrap_width_px word-wraps in that same px
    space (0 = off). align/anchor use the ALIGN_*/ANCHOR_* constants, same semantics as add_text3d."""
    entity[WORLD_TEXT] = {"text": text, "fontId": font_id, "textMaterialId": text_material_id,
                          "renderSizePx": render_size_px, "color": list(color),
                          "align": align, "anchor": anchor, "wrapWidthPx": wrap_width_px}
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
# asset writers: texture stubs + materials
# =============================================================================
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# DXGI_FORMAT values accepted by the texture generator
DXGI_R8G8B8A8_UNORM = 28
DXGI_BC4_UNORM = 80      # grayscale (roughness/AO/height)
DXGI_BC5_UNORM = 83      # normal maps
DXGI_BC6H_UF16 = 95      # HDR
DXGI_BC7_UNORM = 98      # linear color data
DXGI_BC7_UNORM_SRGB = 99 # albedo

def _wtexture_version():
    """Parsed LIVE from texture_format.h; a stale major here means the engine silently rejects the stub."""
    hdr = open(os.path.join(_REPO_ROOT, "src", "engine", "resources", "texture", "texture_format.h"), encoding="utf-8").read()
    major = int(re.search(r"TEXTURE_MAJOR_VERSION\s*=\s*(\d+)", hdr).group(1))
    minor = int(re.search(r"TEXTURE_MINOR_VERSION\s*=\s*(\d+)", hdr).group(1))
    return major, minor

def write_texture_stub(name, source, dxgi_format, mips=True, flip_y=True, out_dir=None):
    """Declares a texture as a header-only .wtexture stub carrying its generation recipe; the engine
    generates it in place at startup (editor builds), preserving this id and name (temp+rename, so a
    crash mid-generate just retries next run). `source` is an image path RELATIVE TO THE STUB's
    directory. Never overwrites an existing generated .wtexture (returns its id instead), so
    generated content is safe to regenerate scripts over. Returns the texture id."""
    if out_dir is None:
        out_dir = os.path.join(_REPO_ROOT, "assets", "textures")
    path = os.path.join(out_dir, name + ".wtexture")
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line == "end_header":
                    break
                if line.startswith("id "):
                    return int(line[3:])
        raise ValueError(f"write_texture_stub: existing {path} has no id line")
    tid = name_id(name)
    major, minor = _wtexture_version()
    stub = (f"wtexture\nversion {major} {minor}\nid {tid}\ncontent_version 0\nname {name}\n"
            f"width 0\nheight 0\nmips 0\ndata_size 0\nuncompressed_size 0\ncompression 0\n"
            f"ungenerated 1\ngen_source {source}\ngen_format {dxgi_format}\n"
            f"gen_mips {1 if mips else 0}\ngen_flip_y {1 if flip_y else 0}\nend_header\n")
    os.makedirs(out_dir, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(stub)
    return tid

_MAT_SAMPLER = {"addressModeU": 0, "addressModeV": 0, "addressModeW": 0, "anisotropyEnable": 0,
                "magFilter": 1, "maxAnisotropy": 1.0, "maxLod": 1000.0, "minFilter": 1,
                "minLod": 0.0, "mipLodBias": 0.0, "mipmapMode": 1}

def write_material(name, base_color=(1.0, 1.0, 1.0, 1.0), metallic=0.0, roughness=1.0,
                   emissive=(0.0, 0.0, 0.0, 0.0),
                   albedo_tex=0, metal_rough_tex=0, normal_tex=0, emissive_tex=0, occlusion_tex=0,
                   lighting_shader="default_pbr_restir", fragment_shader="default_lit",
                   uv_scale=(1.0, 1.0), out_dir=None):
    """Canonical .wmaterial writer (schema pinned from textured.wmaterial + material.cpp Serialize).
    textureRefs slots: 0=albedo, 1=metal-rough, 2=normal, 3=emissive, 4=occlusion, 5=packed NRM
    (material_manager.cpp:129, model_interop.h:155). Texture args take .wtexture ids; stub ids are
    fine, generation preserves the declared id. Serialized textureImageIndices are stale runtime
    caches: -1 here, the live resolve rebuilds them from textureRefs. emissive = [r, g, b, strength].
    Id is name_id(name) so two generators can never collide. Returns the material id."""
    if out_dir is None:
        out_dir = os.path.join(_REPO_ROOT, "assets", "materials")
    mid = name_id(name)
    uv = [uv_scale[0], uv_scale[1], 0.0, 0.0]
    body = {
        "alphaProperties": [0.5, 0.0, 0.0, 0.0],
        "colorFactor": list(base_color),
        "colorUvTransform": list(uv),
        "emissiveFactor": list(emissive),
        "emissiveUvTransform": list(uv),
        "fragmentShader": _fnv1a64(fragment_shader),
        "id": mid,
        "lightingShader": _fnv1a64(lighting_shader),
        "metalRoughFactors": [metallic, roughness, 0.0, 0.0],
        "metalRoughUvTransform": list(uv),
        "name": name,
        "normalUvTransform": list(uv),
        "occlusionUvTransform": list(uv),
        "physicalProperties": [1.5, 0.0, 1.0, 1.0],
        "samplerDesc": [dict(_MAT_SAMPLER) for _ in range(6)],
        "textureImageIndices": [-1, -1, -1, -1],
        "textureImageIndices2": [-1, -1, -1, -1],
        "textureRefs": [albedo_tex, metal_rough_tex, normal_tex, emissive_tex, occlusion_tex, 0],
        "textureSamplerIndices": [2, 2, 2, 2],
        "textureSamplerIndices2": [2, 2, -1, -1],
    }
    header = f"wmaterial\nversion 1 0\nid {mid}\nname {name}\nend_header\n"
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, name + ".wmaterial"), "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.write(json.dumps(body, indent=4))
    return mid

# =============================================================================
# camera / capture shots
# =============================================================================
def camera_look_quat(dx, dy, dz):
    """[w,x,y,z] rotating the CAMERA's forward axis (-Z, camera_system.cpp:61 WORLD_FORWARD)
    onto (dx,dy,dz), Y-up, no roll. This is NOT face_dir(): face_dir aims local +Z (lights,
    cones); a camera given face_dir looks the opposite way."""
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / n, dy / n, dz / n
    pitch = math.asin(max(-1.0, min(1.0, dy)))
    yaw = math.atan2(-dx, -dz)
    cy, sy = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    cp, sp = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    # yawQuat(Y) * pitchQuat(X), same composition UpdateEditorCamera maintains
    return (cy * cp, cy * sp, cp * sy, -sy * sp)


def shot(name, pos, target, settle_frames=None):
    """One capture-run shot: camera at `pos` looking at `target`. Names become <name>.png."""
    d = (target[0] - pos[0], target[1] - pos[1], target[2] - pos[2])
    entry = {"name": name, "translation": list(pos), "rotation": list(camera_look_quat(*d))}
    if settle_frames is not None:
        entry["settleFrames"] = settle_frames
    return entry


def write_shots(path, shots):
    """Shot-list JSON for `will-engine.exe --shots <path>` (capture_shot_system.cpp). Rotation
    is [w,x,y,z] like everything else in this file; build entries with shot()."""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"shots": list(shots)}, f, indent=2)
    return path


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
