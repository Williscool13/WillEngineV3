#!/usr/bin/env python3
"""
Builds the DI density-ladder scene: the measurement instrument for "where does
ReGIR beat WorldGrid binning" (design: project_emissive_group_binning.md).
Run from repo root:  python scripts/build_di_ladder.py

Two SEPARATE ladders (the backends fail by different mechanisms; one ladder
could not attribute the result), each a lab-style grid of open-front 8x4x8
rooms. Flip backends via the ILightProposal switch and compare identical rooms.

  Analytic wing (leftmost columns): N sphere lights, N in {4, 16, 64, 256}.
    Crosses MAX_LIGHTS_PER_WORLD_GRID_CELL=64: the 256-cluster rung overflows
    strongest-K eviction; 64 sits exactly at the cap.
  Emissive wing (middle columns): N emissive box instances (each = 12 triangles =
    one EmissiveGroup), N in {4, 16, 64}. Crosses
    MAX_EMISSIVE_GROUPS_PER_WORLD_GRID_CELL=16 the same way.
  Area wing (rightmost columns): N downward-facing 0.3m rect area lights, same
    rungs as the sphere wing. One-sidedness is the added stressor: a quad
    contributes nothing above its plane, so half of every naive proposal's
    domain is dead, and clustered heroes can be fully occluded by orientation.

Per-wing axes (one room per combination, rows = density rung):
  power distribution: Uniform (all N equal) vs Heavy (one hero carries 50% of
    room power, rest share the remainder). N equal lights at 1/N power make a
    wrong pick too cheap and hide the crossover; the hero makes it expensive.
    Heroes are tinted warm so a starved hero is visible at a glance.
  layout: Clustered (all N inside a 1.8m cube ~ one cascade-0 cell, 2m) vs
    Spread (same N over the whole room, ~1/16 the per-cell density).

TOTAL POWER PER ROOM IS CONSTANT across every rung (decided 2026-07-29):
exposure is global and all rooms share one scene, so climbing brightness would
make noise comparison meaningless. Density and tail are the only variables.
The hero sits at lattice index 0 (a cluster corner), deliberately not centred.

Light range is 6m: covers the room from anywhere inside it, but cannot reach a
neighbour room's cells (pitch 18m), so no rung pollutes another rung's bins.
No sun and no emissive light reps: the ladder lights are the only DI. Skybox
comes from the scene's own SkyboxComponent (the only sky source now).
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("di_ladder")   # must precede every next_id() call

from wscene_authoring import (
    base_entity, box_params, next_id, name_id, add_procedural, add_room,
    add_sphere_light, add_area_light, add_skybox, add_world_text, RENDER_DEFAULTS,
    PROCEDURAL, NAME, FOLDER, SCENE_FOLDER,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT_DIR = os.path.join(REPO, "assets", "materials")
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "di_ladder.wscene")
SCENE_ID = name_id("di_ladder")

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("modern_evening_street_4k")
ROBOTO_FONT = IDX.font("Roboto")
MAT_WALL = IDX.material("lab_grey")
MAT_FLOOR = IDX.material("lab_white")
MAT_PILLAR = IDX.material("lab_white")

# Pipeline-name hashes, NOT asset ids (verified: these reproduce the constants
# hand-pasted into build_lighting_lab.py).
FRAG_SHADER = wa._fnv1a64("default_lit")
LIT_EMISSIVE = wa._fnv1a64("default_pbr")

_SAMPLER = {"addressModeU": 0, "addressModeV": 0, "addressModeW": 0, "anisotropyEnable": 0,
            "magFilter": 1, "maxAnisotropy": 1.0, "maxLod": 1000.0, "minFilter": 1,
            "minLod": 0.0, "mipLodBias": 0.0, "mipmapMode": 1}

def write_emissive_material(name, rgb, strength):
    mid = name_id(name)
    body = {
        "alphaProperties": [0.5, 0.0, 0.0, 0.0],
        "colorFactor": [0.0, 0.0, 0.0, 1.0],
        "colorUvTransform": [1.0, 1.0, 0.0, 0.0],
        "emissiveFactor": [rgb[0], rgb[1], rgb[2], strength],
        "emissiveUvTransform": [1.0, 1.0, 0.0, 0.0],
        "fragmentShader": FRAG_SHADER,
        "id": mid,
        "lightingShader": LIT_EMISSIVE,
        "metalRoughFactors": [0.0, 1.0, 0.0, 0.0],
        "metalRoughUvTransform": [1.0, 1.0, 0.0, 0.0],
        "name": name,
        "normalUvTransform": [1.0, 1.0, 0.0, 0.0],
        "occlusionUvTransform": [1.0, 1.0, 0.0, 0.0],
        "physicalProperties": [1.5, 0.0, 1.0, 1.0],
        "samplerDesc": [dict(_SAMPLER) for _ in range(6)],
        "textureImageIndices": [-1, -1, -1, -1],
        "textureImageIndices2": [-1, -1, -1, -1],
        "textureRefs": [0, 0, 0, 0, 0, 0],
        "textureSamplerIndices": [2, 2, 2, 2],
        "textureSamplerIndices2": [2, 2, -1, -1],
    }
    wa.write_material_file(name, mid, body, MAT_DIR)
    return mid

# =============================================================================
# ladder parameters
# =============================================================================
ROOM = (8.0, 4.0, 8.0)          # interior
WALL_T = 0.25
PITCH = 18.0                    # room-centre spacing; > room + 2*range so bins never cross rooms
LIGHT_RANGE = 6.0
HERO_TINT = (1.0, 0.6, 0.3)
WHITE = (1.0, 1.0, 1.0)
HERO_FRAC = 0.5

ANALYTIC_RUNGS = [4, 16, 64, 256]
EMISSIVE_RUNGS = [4, 16, 64]
AREA_RUNGS = [4, 16, 64, 256]
ANALYTIC_TOTAL = 240.0          # summed sphere-light intensity per room; 4-rung lights land at 60 each, lab-typical
EMISSIVE_TOTAL = 720.0          # summed emissive strength per room (0.25 boxes, ~0.375 m^2 each)
AREA_TOTAL = 240.0              # summed area-light intensity per room
EMITTER_SIZE = 0.25
AREA_HALF_EXTENT = 0.15
DOWN = (0.7071067811865476, 0.7071067811865476, 0.0, 0.0)   # +90 about X: local +Z (quad normal) points -Y

LATTICE = {4: (2, 1, 2), 16: (4, 2, 2), 64: (4, 4, 4), 256: (8, 4, 8)}
CLUSTER_EXTENT = (1.8, 1.8, 1.8)     # fits one 2m cascade-0 cell
CLUSTER_CENTER_Y = 2.2
SPREAD_EXTENT = (6.0, 2.6, 6.0)
SPREAD_CENTER_Y = 1.8

# columns within a wing: (tail, layout)
COMBOS = [("U", "C"), ("U", "S"), ("H", "C"), ("H", "S")]

def lattice_positions(n, center, extent):
    nx, ny, nz = LATTICE[n]
    cx, cy, cz = center
    ex, ey, ez = extent
    def frac(i, count):
        return 0.5 if count == 1 else i / (count - 1)
    pts = []
    for iy in range(ny):
        for iz in range(nz):
            for ix in range(nx):
                pts.append((cx + (frac(ix, nx) - 0.5) * ex,
                            cy + (frac(iy, ny) - 0.5) * ey,
                            cz + (frac(iz, nz) - 0.5) * ez))
    return pts

def powers(n, heavy, total):
    if not heavy:
        return [total / n] * n
    hero = total * HERO_FRAC
    dim = (total - hero) / (n - 1)
    return [hero] + [dim] * (n - 1)

# =============================================================================
# emissive materials: one per (tail, rung) strength, hero shared across rungs
# =============================================================================
EM_MAT = {}
for n in EMISSIVE_RUNGS:
    EM_MAT[("U", n)] = write_emissive_material(f"dl_em_u{n}", WHITE, EMISSIVE_TOTAL / n)
    EM_MAT[("D", n)] = write_emissive_material(f"dl_em_d{n}", WHITE, EMISSIVE_TOTAL * (1.0 - HERO_FRAC) / (n - 1))
EM_MAT["HERO"] = write_emissive_material("dl_em_hero", HERO_TINT, EMISSIVE_TOTAL * HERO_FRAC)
TEXT_MATERIAL = IDX.text_material("default_diegetic")

# =============================================================================
# entity helpers
# =============================================================================
def solid_box(entities, name, corner, size, material, folder_id=0):
    e = base_entity(name, corner, folder_id=folder_id)
    fields, idx = box_params(*size)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = material
    entities.append(e)
    return e

def render_box(entities, name, corner, size, material, folder_id=0):
    e = base_entity(name, corner, folder_id=folder_id)
    fields, idx = box_params(*size)
    e[PROCEDURAL] = {**fields, **RENDER_DEFAULTS, "type": idx}
    e[PROCEDURAL]["material"] = material
    entities.append(e)
    return e

def light_entity(entities, name, pos, folder_id=0, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot, folder_id=folder_id)
    entities.append(e)
    return e

# Unlit world text, NOT emissive Text3D: glyph meshes with an emissive material would
# be gathered as emissive triangle lights and pollute the very selection this scene measures.
LABEL_SIZE_PX = 0.5

def label(entities, name, text, pos, folder_id=0):
    e = base_entity(name, pos, (0.0, 0.0, 1.0, 0.0), folder_id=folder_id)   # 180 about Y: glyph face points -Z, toward the aisle
    add_world_text(e, text, ROBOTO_FONT, TEXT_MATERIAL, scale=LABEL_SIZE_PX)
    entities.append(e)
    return e

def room_shell(entities, tag, cx, cz, folder_id):
    """Open-front room (south face omitted; front faces -Z, toward the camera aisle)."""
    origin = (cx - ROOM[0] * 0.5, 0.0, cz - ROOM[2] * 0.5)
    made = add_room(entities, f"[{tag}]", ROOM, wall_t=WALL_T, origin=origin,
                    faces=("floor", "ceiling", "west", "east", "north"), material=MAT_WALL, folder_id=folder_id)
    made[0][PROCEDURAL]["material"] = MAT_FLOOR
    solid_box(entities, f"[{tag}] Pillar", (cx - 2.2, 0.0, cz - 2.2), (0.4, 3.2, 0.4), MAT_PILLAR, folder_id)

def room_positions(n, clustered, cx, cz):
    if clustered:
        return lattice_positions(n, (cx, CLUSTER_CENTER_Y, cz), CLUSTER_EXTENT)
    return lattice_positions(n, (cx, SPREAD_CENTER_Y, cz), SPREAD_EXTENT)

def analytic_room(entities, tag, cx, cz, n, heavy, clustered, folder_id):
    room_shell(entities, tag, cx, cz, folder_id)
    pts = room_positions(n, clustered, cx, cz)
    pw = powers(n, heavy, ANALYTIC_TOTAL)
    for i, (pos, p) in enumerate(zip(pts, pw)):
        tint = HERO_TINT if (heavy and i == 0) else WHITE
        e = light_entity(entities, f"[{tag}] L{i:03d}", pos, folder_id)
        add_sphere_light(e, color=tint, intensity=p, radius=0.05, draw_range=LIGHT_RANGE, draw_emissive=False)

def area_room(entities, tag, cx, cz, n, heavy, clustered, folder_id):
    room_shell(entities, tag, cx, cz, folder_id)
    pts = room_positions(n, clustered, cx, cz)
    pw = powers(n, heavy, AREA_TOTAL)
    for i, (pos, p) in enumerate(zip(pts, pw)):
        tint = HERO_TINT if (heavy and i == 0) else WHITE
        e = light_entity(entities, f"[{tag}] L{i:03d}", pos, folder_id, rot=DOWN)
        add_area_light(e, color=tint, intensity=p, half_width=AREA_HALF_EXTENT, half_height=AREA_HALF_EXTENT, draw_range=LIGHT_RANGE, draw_emissive=False)

def emissive_room(entities, tag, cx, cz, n, heavy, clustered, folder_id):
    room_shell(entities, tag, cx, cz, folder_id)
    pts = room_positions(n, clustered, cx, cz)
    h = EMITTER_SIZE * 0.5
    for i, pos in enumerate(pts):
        if heavy:
            mat = EM_MAT["HERO"] if i == 0 else EM_MAT[("D", n)]
        else:
            mat = EM_MAT[("U", n)]
        corner = (pos[0] - h, pos[1] - h, pos[2] - h)
        render_box(entities, f"[{tag}] E{i:03d}", corner, (EMITTER_SIZE,) * 3, mat, folder_id)

# =============================================================================
# build
# =============================================================================
entities = []
folders = []
wing_fid = {"Analytic": next_id(), "Emissive": next_id(), "Area": next_id()}

TAIL_NAMES = {"U": "Uniform", "H": "Heavy"}
LAYOUT_NAMES = {"C": "Clustered", "S": "Spread"}

def build_wing(wing, prefix, rungs, col0, builder):
    for row, n in enumerate(rungs):
        for col, (tail, layout) in enumerate(COMBOS):
            tag = f"{prefix}{n}-{tail}{layout}"
            cx = (col0 + col) * PITCH
            cz = row * PITCH
            fid = next_id()
            folders.append({SCENE_FOLDER: {"folderId": fid, "name": tag, "parentFolder": wing_fid[wing]}})
            builder(entities, tag, cx, cz, n, tail == "H", layout == "C", fid)
            text = f"{wing} {n} - {TAIL_NAMES[tail]} {LAYOUT_NAMES[layout]}"
            # Just above the open front, over the entrance the viewer approaches from
            label(entities, f"[{tag}] Label", text, (cx, 4.8, cz - ROOM[2] * 0.5 - 0.5), fid)

build_wing("Analytic", "A", ANALYTIC_RUNGS, -4.5, analytic_room)
build_wing("Emissive", "E", EMISSIVE_RUNGS, 0.5, emissive_room)
build_wing("Area", "AR", AREA_RUNGS, 5.0, area_room)

for wing, fid in wing_fid.items():
    folders.append({SCENE_FOLDER: {"folderId": fid, "name": wing, "parentFolder": 0}})

# shared ground under the wings; top sits 2cm below the room floor slabs (which span -WALL_T..0) so no plane is shared
gx0 = (-4.5 - 0.5) * PITCH - 6.0
gx1 = (5.0 + 3.5) * PITCH + 6.0
gz0 = -10.0
gz1 = 3 * PITCH + 12.0
solid_box(entities, "Ground", (gx0, -WALL_T - 0.02 - 0.5, gz0), (gx1 - gx0, 0.5, gz1 - gz0), MAT_WALL)

# intensity 0: sky stays visible for orientation but contributes zero IBL, so the ladder lights are the only DI
sky = base_entity("Skybox", (0.0, 0.0, 0.0))
add_skybox(sky, ENV_MAP, intensity=0.0, priority=0)
entities.append(sky)

entities.extend(folders)

editor_camera = {"rotation": [0.0, 0.0, 1.0, 0.0], "translation": [-2.0 * PITCH, 5.0, -14.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "DI Ladder", editor_camera=editor_camera)

n_spheres = sum(1 for e in entities if wa.LIGHT_SPHERE in e)
n_areas = sum(1 for e in entities if wa.LIGHT_AREA in e)
n_emitters = sum(1 for e in entities if PROCEDURAL in e and e[NAME]["name"].startswith("[E") and "] E" in e[NAME]["name"])
n_rooms = (len(ANALYTIC_RUNGS) + len(EMISSIVE_RUNGS) + len(AREA_RUNGS)) * len(COMBOS)
print(f"wrote {SCENE_PATH} ({len(entities)} entities, {n_rooms} rooms)")
print(f"analytic lights: {n_spheres} sphere + {n_areas} area (peak clustered cell {max(ANALYTIC_RUNGS)} vs MAX_LIGHTS_PER_WORLD_GRID_CELL=64)")
print(f"emissive groups: {n_emitters} of MAX_EMISSIVE_GROUPS=1024 (peak clustered cell {max(EMISSIVE_RUNGS)} vs per-cell cap 16)")
print(f"vf.lights estimate: {n_spheres + n_areas} analytic + {n_emitters * 12} emissive tris = {n_spheres + n_areas + n_emitters * 12} of MAX_LIGHTS=32768")
