#!/usr/bin/env python3
"""
Builds the DI architecture scene: the qualitative companion to di_ladder.
Run from repo root:  python scripts/build_di_arch.py

Two parts:

  Sibenik shell: the imported cathedral (sibenik.wsmesh) under a mixed analytic
    rig: nave chandeliers, aisle candles, an apse feature light, and cool area
    lights placed just inside the clerestory as fake window daylight (the glass
    is opaque; the engine has no transparency). Real occlusion structure
    (columns, vaults, apse) is what the ladder's boxes cannot provide.

  Aperture sweep wing (the instrument): 5 sealed rooms, each lit ONLY by a
    constant-power area panel in a chamber behind a WallParams wall. Opening
    area sweeps 6.00 / 1.44 / 0.40 / 0.09 m2, plus a lattice room with 8 small
    holes matching the window's total area (many-aperture vs one at equal
    area). The grid bin is range-overlap and occlusion-blind, so every
    receiving-room cell proposes the panel at full weight regardless of
    aperture; the sweep measures how candidate wastage, reuse-island size and
    denoiser behaviour degrade as the lit solid angle shrinks.

Panels use draw_emissive=True: light reps are injected as primitive instances,
not MeshRuntime entities, so the emissive-triangle gather never sees them (no
DI double-count even with bEmissiveTriangleLights on).

Room pitch 16m with panel range 10m: a panel's influence sphere cannot touch a
neighbour room's cells, so each rung's bins stay isolated. No sun; skybox at
0.1 for orientation without drowning the rigs.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("di_arch")   # must precede every next_id() call

from wscene_authoring import (
    base_entity, box_params, wall_params, lattice_params, corrugated_params,
    next_id, name_id, add_procedural, add_path_mover, module_part, add_module,
    add_static_mesh, add_sphere_light, add_area_light, add_skybox, add_world_text,
    add_reflection_probe, add_prefab_instance, basis_for, mat_to_quat,
    RENDER_DEFAULTS, PROCEDURAL, PHYSICS, SCENE_FOLDER, PROBE_BOX, EASE_IN_OUT_SINE,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "di_arch.wscene")
SCENE_ID = name_id("di_arch")

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("modern_evening_street_4k")
ROBOTO_FONT = IDX.font("Roboto")
MAT_WALL = IDX.material("lab_grey")
MAT_FLOOR = IDX.material("lab_white")
TEXT_MATERIAL = IDX.text_material("default_diegetic")
SIBENIK_MODEL = IDX.model("sibenik")
BARREL_PREFAB = IDX.prefab("Barrel")
BARREL_MATERIAL = 4805580789092568935  # from barrel.wprefab's own procedural payload

LIT_RESTIR = wa._fnv1a64("default_pbr_restir")

WALL_T = 0.25
GROUND_TOP = -WALL_T - 0.02      # 2cm below every floor slab, ladder convention

WARM = (1.0, 0.85, 0.6)
CANDLE = (1.0, 0.6, 0.3)
DAYLIGHT = (0.75, 0.85, 1.0)
WHITE = (1.0, 1.0, 1.0)

entities = []
folders = []

def folder(name, parent=0):
    fid = next_id()
    folders.append({SCENE_FOLDER: {"folderId": fid, "name": name, "parentFolder": parent}})
    return fid

def solid_box(name, corner, size, material, folder_id=0):
    e = base_entity(name, corner, folder_id=folder_id)
    fields, idx = box_params(*size)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = material
    entities.append(e)
    return e

def light_entity(name, pos, folder_id=0, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot, folder_id=folder_id)
    entities.append(e)
    return e

def facing(direction):
    """Rotation whose local +Z (area-light normal) points along `direction`."""
    right, up, fwd, _ = basis_for((0.0, 0.0, 0.0), direction)
    return mat_to_quat(right, up, fwd)

LABEL_SIZE_PX = 0.5

def label(name, text, pos, folder_id=0):
    e = base_entity(name, pos, (0.0, 0.0, 1.0, 0.0), folder_id=folder_id)   # 180 about Y: glyph face points -Z, toward the aisle
    add_world_text(e, text, ROBOTO_FONT, TEXT_MATERIAL, scale=LABEL_SIZE_PX)
    entities.append(e)
    return e

# =============================================================================
# Sibenik shell
# sibenik.obj spans x -20.14..20.14 (nave axis), y -15.31..15.30, z -8.5..8.5,
# metres, identity through the glb conversion. Placed so the interior floor
# lands at GROUND_TOP + 0.02 = -0.25, matching the wing's floor slabs.
# =============================================================================
SIBENIK_FLOOR = -15.31
SHELL_Y = -SIBENIK_FLOOR - 0.25

fid_sibenik = folder("Sibenik")

shell = base_entity("Sibenik Cathedral", (0.0, SHELL_Y, 0.0), folder_id=fid_sibenik)
add_static_mesh(shell, SIBENIK_MODEL, lighting_shader=LIT_RESTIR)
entities.append(shell)

# Nave chandeliers along the long axis. Emitters are deliberately fat: near-point
# sources blotch/boil at this sample budget, and that stress is the ladder's job, not this scene's.
for i, x in enumerate(range(-15, 16, 5)):
    e = light_entity(f"Chandelier {i}", (float(x), 6.0, 0.0), fid_sibenik)
    add_sphere_light(e, color=WARM, intensity=140.0, radius=0.25, draw_range=14.0, draw_emissive=True)

# Aisle candles, one line per side
ci = 0
for side in (-1.0, 1.0):
    for x in range(-14, 15, 4):
        e = light_entity(f"Candle {ci}", (float(x), 1.6, side * 6.2), fid_sibenik)
        add_sphere_light(e, color=CANDLE, intensity=12.0, radius=0.12, draw_range=5.0, draw_emissive=True)
        ci += 1

# Apse feature: one broad panel washing back down the nave, two candle stands
e = light_entity("Apse Panel", (16.0, 5.0, 0.0), fid_sibenik, rot=facing((-1.0, 0.0, 0.0)))
add_area_light(e, color=(0.9, 0.95, 1.0), intensity=220.0, half_width=1.5, half_height=1.0, draw_range=18.0, draw_emissive=True)
for k, z in enumerate((-2.0, 2.0)):
    e = light_entity(f"Apse Candle {k}", (16.0, 1.2, z), fid_sibenik)
    add_sphere_light(e, color=CANDLE, intensity=25.0, radius=0.12, draw_range=6.0, draw_emissive=True)

# Window fills: cool panels hung in the open nave volume above the arcade, aimed
# down-outward 45 like light shafts falling from the clerestory. Free air is the only
# placement that survives Sibenik's interior shells (aisle/chapel vaults hollow out the
# wide body; the nave core walls sit at |z| ~3.5-4 with the vault above): the nave axis
# volume is proven open by the chandeliers. Corners swing 0.49 in z: 2.8 + 0.49 < 3.5.
wi = 0
for side in (-1.0, 1.0):
    rot = facing((0.0, -1.0, -side))
    for x in (-10.0, 0.0, 10.0):
        e = light_entity(f"Window Fill {wi}", (x, 13.0, side * 2.8), fid_sibenik, rot=rot)
        add_area_light(e, color=DAYLIGHT, intensity=90.0, half_width=0.5, half_height=0.7, draw_range=15.0, draw_emissive=True)
        wi += 1

# Barrel props in the south aisle: prefab provenance + the prefab's own components
for bi, (bx, bz) in enumerate(((7.0, 5.2), (8.3, 5.6), (7.6, 4.1), (9.0, 4.6))):
    e = base_entity(f"Barrel {bi}", (bx - 0.5, -0.25, bz - 0.5), folder_id=fid_sibenik)
    fields, idx = box_params(1.0, 1.0, 1.0)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = BARREL_MATERIAL
    add_prefab_instance(e, BARREL_PREFAB)
    entities.append(e)

# Interior probe: box over the whole shell, captured from the nave centre
probe = base_entity("Sibenik Probe", (0.0, 15.0, 0.0), scale=(21.0, 16.0, 9.0), folder_id=fid_sibenik)
add_reflection_probe(probe, name_id("di_arch_sibenik"), shape=PROBE_BOX, capture_offset=(0.0, -12.0, 0.0))
entities.append(probe)

# =============================================================================
# Aperture sweep wing
# =============================================================================
ROOM_W, ROOM_H, ROOM_D = 6.0, 4.0, 6.0    # receiving room interior
CHAMBER_D = 2.0                            # light chamber interior depth
Z0 = 41.0                                  # receiving room front plane (open, faces -Z)
PITCH = 16.0
PANEL_INTENSITY = 400.0
PANEL_RANGE = 10.0

RUNGS = [
    ("Doorway", [(2.0, 0.0, 2.0, 3.0)]),
    ("Window",  [(2.4, 1.4, 1.2, 1.2)]),
    ("Slit",    [(2.0, 1.8, 2.0, 0.2)]),
    ("Crack",   [(2.85, 1.85, 0.3, 0.3)]),
    ("Lattice", [(1.2 + col * 1.0, y, 0.42, 0.42) for y in (1.2, 2.4) for col in range(4)]),
]

def opening_area(openings):
    return sum(w * h for _, _, w, h in openings)

fid_wing = folder("Aperture Sweep")

def aperture_room(tag, cx, openings, folder_id):
    x0 = cx - ROOM_W * 0.5
    z_wall = Z0 + ROOM_D                       # aperture wall front face
    z_back = z_wall + WALL_T + CHAMBER_D       # chamber back wall front face
    depth = ROOM_D + WALL_T + CHAMBER_D        # open front .. chamber back (interior)
    # One envelope for room + chamber: no coplanar overlaps anywhere
    solid_box(f"[{tag}] floor", (x0 - WALL_T, -WALL_T, Z0 - WALL_T), (ROOM_W + 2 * WALL_T, WALL_T, depth + 2 * WALL_T), MAT_FLOOR, folder_id)
    solid_box(f"[{tag}] ceiling", (x0 - WALL_T, ROOM_H, Z0 - WALL_T), (ROOM_W + 2 * WALL_T, WALL_T, depth + 2 * WALL_T), MAT_WALL, folder_id)
    solid_box(f"[{tag}] west", (x0 - WALL_T, 0.0, Z0 - WALL_T), (WALL_T, ROOM_H, depth + 2 * WALL_T), MAT_WALL, folder_id)
    solid_box(f"[{tag}] east", (x0 + ROOM_W, 0.0, Z0 - WALL_T), (WALL_T, ROOM_H, depth + 2 * WALL_T), MAT_WALL, folder_id)
    solid_box(f"[{tag}] back", (x0, 0.0, z_back), (ROOM_W, ROOM_H, WALL_T), MAT_WALL, folder_id)

    e = base_entity(f"[{tag}] aperture wall", (x0, 0.0, z_wall), folder_id=folder_id)
    fields, idx = wall_params(ROOM_W, ROOM_H, WALL_T, openings)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = MAT_WALL
    entities.append(e)

    e = light_entity(f"[{tag}] panel", (cx, 2.0, z_back - 0.1), folder_id, rot=(0.0, 0.0, 1.0, 0.0))   # 180 about Y: normal -Z, at the aperture
    add_area_light(e, color=WHITE, intensity=PANEL_INTENSITY, half_width=2.5, half_height=1.6, draw_range=PANEL_RANGE, draw_emissive=True)

    pr = base_entity(f"[{tag}] probe", (cx, ROOM_H * 0.5, Z0 + depth * 0.5),
                     scale=(ROOM_W * 0.5 + WALL_T, ROOM_H * 0.5 + WALL_T, depth * 0.5 + WALL_T), folder_id=folder_id)
    add_reflection_probe(pr, name_id(f"di_arch_{tag.lower()}"), shape=PROBE_BOX)
    entities.append(pr)

for i, (tag, openings) in enumerate(RUNGS):
    cx = (i - 2) * PITCH
    fid = folder(tag, fid_wing)
    aperture_room(tag, cx, openings, fid)
    text = f"{tag} - {len(openings)} opening{'s' if len(openings) > 1 else ''}, {opening_area(openings):.2f} m2"
    label(f"[{tag}] Label", text, (cx, 4.7, Z0 - 0.7), fid)

label("Wing Label", f"Aperture Sweep - constant panel power {PANEL_INTENSITY:.0f}", (0.0, 6.2, Z0 - 0.7), fid_wing)

# =============================================================================
# Phase 2 props: new shapes + a path mover, on the ground between chapel and wing
# =============================================================================
fid_props = folder("Phase 2 Props")
PROP_Y = GROUND_TOP

e = base_entity("Mast X-Brace", (-12.3, PROP_Y, 19.7), folder_id=fid_props)
fields, idx = lattice_params(0.6, 6.0, 0.6, chord=0.08, brace=0.05, bays=6, pattern=0)
add_procedural(e, idx, fields)
e[PROCEDURAL]["material"] = MAT_WALL
entities.append(e)

e = base_entity("Mast Single-Diagonal", (-6.3, PROP_Y, 19.7), folder_id=fid_props)
fields, idx = lattice_params(0.6, 6.0, 0.6, chord=0.08, brace=0.05, bays=6, pattern=1)
add_procedural(e, idx, fields)
e[PROCEDURAL]["material"] = MAT_WALL
entities.append(e)

# Horizontal truss between the masts: -90 about Z maps local +Y (long axis) to world +X;
# local X then points -Y, so the envelope hangs 0.6 below the origin
e = base_entity("Truss Beam", (-11.7, 4.3, 19.7), rot=(0.7071068, 0.0, 0.0, -0.7071068), folder_id=fid_props)
fields, idx = lattice_params(0.6, 5.4, 0.6, chord=0.06, brace=0.04, bays=5, pattern=0)
add_procedural(e, idx, fields)
e[PROCEDURAL]["material"] = MAT_WALL
entities.append(e)

for name, cx, kwargs in (
    ("Panel Default", 4.0, {}),
    ("Panel Chunky", 7.0, {"rib_depth": 0.12, "rib_width": 0.4, "rib_count": 4}),
    ("Panel Fine", 10.0, {"rib_depth": 0.03, "rib_width": 0.1, "rib_count": 12}),
):
    e = base_entity(name, (cx, PROP_Y, 20.0), folder_id=fid_props)
    fields, idx = corrugated_params(2.4, 2.4, **kwargs)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = MAT_FLOOR
    entities.append(e)

# Kinematic patrol platform exercising add_path_mover
e = base_entity("Patrol Platform", (-2.0, 1.0, 26.0), folder_id=fid_props)
fields, idx = box_params(2.0, 0.3, 2.0)
add_procedural(e, idx, fields, motion=1)
e[PROCEDURAL]["material"] = MAT_FLOOR
add_path_mover(e, [(-2.0, 1.0, 26.0), (6.0, 1.0, 26.0), (6.0, 3.0, 30.0)],
               speed=2.0, wait_time=0.5, easing=EASE_IN_OUT_SINE)
entities.append(e)

# Kiosk module: structure on slot 0, trim + finial on slot 1. Two instances with swapped
# slot materials share ONE content-hashed model and re-skin at resolve time.
KIOSK_PARTS = [
    module_part(wall_params(3.0, 3.0, 0.2, [(0.9, 1.0, 1.2, 1.2)]), slot=0),
    module_part(box_params(0.2, 3.0, 1.5), offset=(0.0, 0.0, 0.2), slot=0),
    module_part(box_params(0.2, 3.0, 1.5), offset=(2.8, 0.0, 0.2), slot=0),
    module_part(box_params(3.4, 0.2, 2.1), offset=(-0.2, 3.0, -0.2), slot=1),
    module_part(box_params(1.6, 0.1, 0.15), offset=(0.7, 0.9, -0.15), slot=1),
    module_part(box_params(1.6, 0.15, 0.15), offset=(0.7, 2.2, -0.15), slot=1),
    module_part(lattice_params(0.3, 1.5, 0.3, chord=0.04, brace=0.03, bays=3), offset=(1.35, 3.2, 0.7), slot=1),
]
for name, cx, mats in (("Kiosk A", 13.5, [MAT_WALL, MAT_FLOOR]), ("Kiosk B", 17.5, [MAT_FLOOR, MAT_WALL])):
    e = base_entity(name, (cx, PROP_Y, 19.0), folder_id=fid_props)
    add_module(e, KIOSK_PARTS, materials=mats)
    entities.append(e)

label("Props Label", "Phase 2 Props - Lattice, Corrugated Panel, Path Mover, Module", (-1.0, 5.5, 18.5), fid_props)

# =============================================================================
# ground, sky, camera
# =============================================================================
solid_box("Ground", (-55.0, GROUND_TOP - 0.5, -25.0), (110.0, 0.5, 85.0), MAT_WALL)

sky = base_entity("Skybox", (0.0, 0.0, 0.0))
add_skybox(sky, ENV_MAP, intensity=0.1, priority=0)
entities.append(sky)

entities.extend(folders)

# Start inside the nave looking down it (+X); [w,x,y,z], -90 about Y
editor_camera = {"rotation": [0.7071068, 0.0, -0.7071068, 0.0], "translation": [-16.0, 4.0, 0.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "DI Architecture", editor_camera=editor_camera)

n_spheres = sum(1 for e in entities if wa.LIGHT_SPHERE in e)
n_areas = sum(1 for e in entities if wa.LIGHT_AREA in e)
n_probes = sum(1 for e in entities if wa.PROBE in e)
for tag, openings in RUNGS:
    for x, y, w, h in openings:
        assert 0.0 <= x and x + w <= ROOM_W and 0.0 <= y and y + h <= ROOM_H, f"{tag} opening out of wall bounds"
    assert len(openings) <= 8, tag
print(f"wrote {SCENE_PATH} ({len(entities)} entities)")
print(f"lights: {n_spheres} sphere + {n_areas} area; probes: {n_probes} (unbaked)")
print(f"aperture areas: " + ", ".join(f"{tag} {opening_area(o):.2f} m2" for tag, o in RUNGS))
