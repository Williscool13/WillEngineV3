#!/usr/bin/env python3
"""
Builds the GI sun-bounce scene: assets/scenes/gi_sunbounce.wscene.
Run from repo root:  python scripts/build_gi_sunbounce.py

The fail case under test: the dominant light source reaches the interior only through GI
(sun bounces off exterior ground / a small patch, everything else is 1+ bounce). One global
sun, elevation 50 deg, azimuth 15 deg west of the door axis; skybox stays ON at 1.0 (sun and
sky intensity are the live rungs to slide in-editor). All-white surfaces so chroma cannot
mask noise. One box reflection probe per room interior: without probes fully enclosed
rooms leak (known issue, probes are the sanctioned indoor fallback tier); bake them after
loading the scene.

Walls are 1.0 thick. The gi_stress ladder threshold (0.5) does NOT transfer here: the
sun+sky differential hits every face and distant rooms sit on coarser DDGI cascades, and
this scene measures indirect NOISE, so residual wall leak is just contamination. Each room
is ONE ModuleMesh
entity (parts share slot 0) with a mirrored per-part collider PhysicsBodyDesc; the
PhysicsBodyDesc shape cap is 8, so a room is 7 shell parts + 1 judging cube, and each
hangar splits into a shell module and a props module.

S row (z 0..8), one 8x4.5x8 sealed room per rung, door orientation is the variable:
  S1 Door Patch  - door faces the sun: direct pool at the threshold (control)
  S2 Clerestory  - high window strip: detached sun pool mid-floor, walls indirect
  S3 Side Door   - west door, sun cannot enter: fed only by the sunlit apron (THE fail case)
  S4 Lee Door    - north door in the building's own shadow: sky + second bounce only

I row: S3's setup with a door ladder 3.0 / 1.5 / 0.7 - smaller aperture, less indirect flux,
more noise.

Hangars (z 20..40, 20x8x20):
  L1 Hangar Front - 6x5 door faces the sun: ~4m patch inside, 15m of indirect behind it
  L2 Hangar Side  - 6x5 west door: fully indirect at scale (the motivating case)
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("gi_sunbounce")   # must precede every next_id() call

from wscene_authoring import (
    base_entity, box_params, cylinder_params, wall_params, module_part, add_module,
    next_id, name_id, add_procedural, add_directional_light, add_skybox, add_world_text,
    add_reflection_probe, add_local_ddgi_volume, PROBE_RES_128, PROBE_RES_256,
    write_material, PROCEDURAL, PHYSICS, SCENE_FOLDER,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "gi_sunbounce.wscene")
SCENE_ID = name_id("gi_sunbounce")

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("modern_evening_street_4k")
ROBOTO_FONT = IDX.font("Roboto")
TEXT_MATERIAL = IDX.text_material("default_diegetic")

# Same definition as build_gi_stress.py: identical args -> same id, same file.
MAT_WHITE = write_material("gi_white", base_color=(0.73, 0.73, 0.73, 1.0))

WALL_T = 1.0                                   # 0.5 (the ladder threshold) still leaked here: sun+sky differential on every face + coarser cascades at distance
ROT_WEST = (0.7071068, 0.0, 0.7071068, 0.0)    # +90 about Y: slab thickness (+Z local) -> +X

# Sun: elevation 50, azimuth 15 deg west of the +Z travel axis. dx < 0 keeps west doors
# strictly direct-free; dz > 0 lets south doors admit the patch.
EL = math.radians(50.0)
AZ = math.radians(15.0)
SUN_DIR = (-math.sin(AZ) * math.cos(EL), -math.sin(EL), math.cos(AZ) * math.cos(EL))

def face_dir(dx, dy, dz):
    """[w,x,y,z] quat mapping local +Z to (dx,dy,dz); the sun shines along its local +Z."""
    if dz < -0.999999:
        return [0.0, 0.0, 1.0, 0.0]
    cx, cy = -dy, dx
    w = 1.0 + dz
    n = math.sqrt(w * w + cx * cx + cy * cy)
    return [w / n, cx / n, cy / n, 0.0]

entities = []
folders = []

def folder(name, parent=0):
    fid = next_id()
    folders.append({SCENE_FOLDER: {"folderId": fid, "name": name, "parentFolder": parent}})
    return fid

def solid_box(name, corner, size, folder_id=0):
    e = base_entity(name, corner, folder_id=folder_id)
    fields, idx = box_params(*size)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = MAT_WHITE
    entities.append(e)
    return e

def module_entity(name, origin, parts, folder_id):
    """One module (all parts slot 0, white) + a mirrored per-part collider body (cap 8)."""
    if len(parts) > 8:
        raise ValueError(f"{name}: {len(parts)} parts exceeds the PhysicsBodyDesc shape cap of 8")
    e = base_entity(name, origin, folder_id=folder_id)
    add_module(e, parts, materials=(MAT_WHITE,))
    shapes = []
    for p in parts:
        fields = {k: v for k, v in p.items() if k not in ("type", "offset", "rotation", "slot")}
        shapes.append({**fields, "type": 3, "offset": p["offset"], "rotation": p["rotation"],
                       "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0,
                       "meshSourceModelId": 0, "proceduralType": p["type"]})
    e[PHYSICS] = {"motionType": 0, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                  "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": shapes}
    entities.append(e)
    return e

def label(name, text, pos, folder_id=0):
    e = base_entity(name, pos, (0.0, 0.0, 1.0, 0.0), folder_id=folder_id)   # faces the -Z aisle
    add_world_text(e, text, ROBOTO_FONT, TEXT_MATERIAL, render_size_px=0.5,
                   align=wa.ALIGN_CENTER, anchor=wa.ANCHOR_BOTTOM)
    entities.append(e)
    return e

def room_parts(inner, door_face, door_w, door_h, sill, judging_cube=True):
    """Sealed-room part list, local origin at the outer min corner (floor bottom). door_face:
    'south'(-Z), 'north'(+Z), 'west'(-X), 'clerestory' (south wall, high strip). Openings are
    horizontally centered, so the rotated west wall's mirrored local-x axis cannot misplace them."""
    sx, sy, sz = inner
    t = WALL_T
    width = sx + 2 * t
    parts = [
        module_part(box_params(width, t, sz + 2 * t)),
        module_part(box_params(width, t, sz + 2 * t), offset=(0.0, t + sy, 0.0)),
    ]
    if door_face in ("south", "clerestory"):
        parts.append(module_part(wall_params(width, sy, t, openings=[((width - door_w) * 0.5, sill, door_w, door_h)]), offset=(0.0, t, 0.0)))
    else:
        parts.append(module_part(box_params(width, sy, t), offset=(0.0, t, 0.0)))
    if door_face == "north":
        parts.append(module_part(wall_params(width, sy, t, openings=[((width - door_w) * 0.5, sill, door_w, door_h)]), offset=(0.0, t, t + sz)))
    else:
        parts.append(module_part(box_params(width, sy, t), offset=(0.0, t, t + sz)))
    if door_face == "west":
        parts.append(module_part(wall_params(sz, sy, t, openings=[((sz - door_w) * 0.5, sill, door_w, door_h)]), offset=(0.0, t, t + sz), rotation=ROT_WEST))
    else:
        parts.append(module_part(box_params(t, sy, sz), offset=(0.0, t, t)))
    parts.append(module_part(box_params(t, sy, sz), offset=(t + sx, t, t)))
    if judging_cube:
        # Off-center so the indirect light has a contact shadow to resolve.
        parts.append(module_part(box_params(1.2, 1.2, 1.2), offset=(t + sx * 0.65, t, t + sz * 0.6)))
    return parts

def sealed_room(tag, cx, cz, inner, door_face, door_w, door_h, sill, folder_id, judging_cube=True):
    """Interior floor top at y=0, interior center (cx, cz). Box probe spans the OUTER shell:
    inner-face-aligned bounds flicker under jitter (wall pixels sit exactly on the boundary and
    flip between probe and skybox). Capture at mid-height clears the judging cube.
    Local DDGI volume per room, bounds ~1 cell past the outer shell so the edge fade band lies
    outside the interior; spacing picked so counts stay under the 16/axis window clamp."""
    sx, sy, sz = inner
    origin = (cx - sx * 0.5 - WALL_T, -WALL_T, cz - sz * 0.5 - WALL_T)
    room = module_entity(f"[{tag}] room", origin, room_parts(inner, door_face, door_w, door_h, sill, judging_cube), folder_id)
    probe = base_entity(f"[{tag}] probe", (cx, sy * 0.5, cz), scale=(sx * 0.5 + WALL_T, sy * 0.5 + WALL_T, sz * 0.5 + WALL_T), folder_id=folder_id)
    resolution = PROBE_RES_256 if sx > 10.0 else PROBE_RES_128
    add_reflection_probe(probe, name_id(f"gi_sunbounce_{tag}"), resolution=resolution)
    entities.append(probe)
    spacing = 0.8 if sx <= 10.0 else 1.6
    volume = base_entity(f"[{tag}] gi volume", (cx, sy * 0.5, cz), scale=(sx * 0.5 + WALL_T + 0.9, sy * 0.5 + WALL_T + 1.0, sz * 0.5 + WALL_T + 0.9), folder_id=folder_id)
    add_local_ddgi_volume(volume, name_id(f"gi_sunbounce_lv_{tag}"), probe_spacing=spacing)
    entities.append(volume)
    return room

# =============================================================================
# S row: door orientation rungs (interior z 0..8)
# =============================================================================
S_INNER = (8.0, 4.5, 8.0)
S_CZ = 4.0

def build_s_row():
    fid_row = folder("S - Orientation Rungs")
    specs = [
        ("S1", -45.0, "south", 2.5, 3.0, 0.0, "Door Patch - direct sun pool at the threshold, rest is bounce"),
        ("S2", -30.0, "clerestory", 4.0, 0.9, 3.2, "Clerestory - detached sun pool mid-floor, walls indirect"),
        ("S3", -15.0, "west", 2.5, 3.0, 0.0, "Side Door - no direct entry, fed by the sunlit apron"),
        ("S4", 0.0, "north", 2.5, 3.0, 0.0, "Lee Door - apron in building shadow, sky + second bounce"),
    ]
    for tag, cx, face, w, h, sill, text in specs:
        fid = folder(tag, fid_row)
        sealed_room(tag, cx, S_CZ, S_INNER, face, w, h, sill, fid)
        label(f"[{tag}] Label", f"{tag} {text}", (cx, 5.6, -1.2), fid)

# =============================================================================
# I row: indirect aperture ladder (all west doors, no direct entry possible)
# =============================================================================
def build_i_row():
    fid_row = folder("I - Indirect Ladder")
    specs = [("I1", 15.0, 3.0, 3.0), ("I2", 30.0, 1.5, 2.2), ("I3", 45.0, 0.7, 1.2)]
    for tag, cx, w, h in specs:
        fid = folder(tag, fid_row)
        sealed_room(tag, cx, S_CZ, S_INNER, "west", w, h, 0.0, fid)
        label(f"[{tag}] Label", f"{tag} Indirect {w:.1f} x {h:.1f} door", (cx, 5.6, -1.2), fid)

# =============================================================================
# Hangars (interior z 20..40): shell module + props module (shape cap is 8 per body)
# =============================================================================
L_INNER = (20.0, 8.0, 20.0)
L_CZ = 30.0

def hangar_props(tag, cx, cz, fid):
    parts = []
    for dx, dz, s in [(-6.0, -4.0, 2.0), (-2.5, 2.0, 1.4), (4.0, -2.0, 1.4), (6.0, 5.0, 2.4), (0.5, 6.5, 1.0)]:
        parts.append(module_part(box_params(s, s, s), offset=(dx, 0.0, dz)))
    for dx, dz in [(-4.0, 6.0), (3.0, 4.0)]:
        parts.append(module_part(cylinder_params(0.35, L_INNER[1], slices=20), offset=(dx, L_INNER[1] * 0.5, dz)))
    module_entity(f"[{tag}] props", (cx, 0.0, cz), parts, fid)

def build_hangars():
    fid_row = folder("L - Hangars")
    fid = folder("L1", fid_row)
    sealed_room("L1", -14.0, L_CZ, L_INNER, "south", 6.0, 5.0, 0.0, fid, judging_cube=False)
    hangar_props("L1", -14.0, L_CZ, fid)
    label("[L1] Label", "L1 Hangar Front - patch at the door, 15m of indirect behind", (-14.0, 9.6, 18.3), fid)

    fid = folder("L2", fid_row)
    sealed_room("L2", 16.0, L_CZ, L_INNER, "west", 6.0, 5.0, 0.0, fid, judging_cube=False)
    hangar_props("L2", 16.0, L_CZ, fid)
    label("[L2] Label", "L2 Hangar Side - fully indirect at scale (the motivating case)", (16.0, 9.6, 18.3), fid)

# =============================================================================
# build
# =============================================================================
build_s_row()
build_i_row()
build_hangars()

# White ground everywhere: the sunlit apron outside each door IS the bounce source.
solid_box("Ground", (-55.0, -WALL_T - 0.52, -12.0), (110.0, 0.5, 58.0))

sun = base_entity("Sun", (0.0, 25.0, -15.0), tuple(face_dir(*SUN_DIR)))
add_directional_light(sun, color=(1.0, 0.96, 0.88), intensity=5.0, priority=0, angular_radius_deg=0.5)
entities.append(sun)

sky = base_entity("Skybox", (0.0, 0.0, 0.0))
add_skybox(sky, ENV_MAP, intensity=1.0, priority=0)
entities.append(sky)

entities.extend(folders)

# 180 about Y at the aisle: look +Z into the S/I row fronts.
editor_camera = {"rotation": [0.0, 0.0, 1.0, 0.0], "translation": [0.0, 5.0, -16.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "GI Sun Bounce", editor_camera=editor_camera)
n_modules = sum(1 for e in entities if wa.MODULE in e)
print(f"wrote {SCENE_PATH} ({len(entities)} entities, {n_modules} modules)")
print(f"sun dir {tuple(round(c, 4) for c in SUN_DIR)} (el 50, az 15 west); sun 5.0 / sky 1.0 are the rungs to slide")
