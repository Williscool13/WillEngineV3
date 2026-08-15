#!/usr/bin/env python3
"""
Builds the GI stress scene: assets/scenes/gi_stress.wscene + its materials.
Run from repo root:  python scripts/build_gi_stress.py

Two rows of open-front rooms (front faces -Z, camera aisle between the rows), skybox at 0 so the
authored lights are the only energy. No reflection probes on purpose: this scene measures the
dynamic GI stack (DDGI + radiance cache + gather) without the baked fallback tier masking it.

Row A (z=0), chroma:
  A1 Cornell White    - all-white reference box, one ceiling panel, tall+short blockers
  A2 Cornell Classic  - red left / green right walls: canonical single-bounce tint check
  A3 Cornell Saturated- red/green/blue walls + clay floor: multi-bounce saturation vs GT
  A4 Emissive Only    - zero analytic lights; two large dim emissive ceiling slabs (the TDA
                        posture: big dim emitters ride GI, not DI)

Row B (z=20), transport:
  B1 Bounce Corridor  - L-corridor, light hidden in the far leg; everything visible from the
                        front is 1+ bounce, red brick walls tint it
  B2 Leak Ladder      - dark closets sharing a wall with a blasted-bright sealed room; shared
                        wall thins 2.0 / 1.0 / 0.5 / 0.25 / 0.10 / 0.05 left to right; any light = leak
  B3 Pillar Court     - cylinder grid + lattice masts under one panel: nearfield occlusion
                        variance (the Sponza-arcade failure shape)
  B4 Material Court   - all six Poly Haven PBR sets: quartered floor, brick/plaster walls,
                        spheres + boxes, warm key + cool fill
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("gi_stress")   # must precede every next_id() call

from wscene_authoring import (
    base_entity, box_params, cylinder_params, sphere_params, lattice_params,
    next_id, name_id, add_procedural, add_area_light, add_skybox, add_world_text,
    write_material, RENDER_DEFAULTS, PROCEDURAL, SCENE_FOLDER,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "gi_stress.wscene")
SCENE_ID = name_id("gi_stress")

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("modern_evening_street_4k")
ROBOTO_FONT = IDX.font("Roboto")
TEXT_MATERIAL = IDX.text_material("default_diegetic")
MAT_GREY = IDX.material("lab_grey")

# Flat chroma materials (classic Cornell albedos) + dim emissive panels
MAT_WHITE = write_material("gi_white", base_color=(0.73, 0.73, 0.73, 1.0))
MAT_RED = write_material("gi_red", base_color=(0.63, 0.065, 0.05, 1.0))
MAT_GREEN = write_material("gi_green", base_color=(0.14, 0.45, 0.091, 1.0))
MAT_BLUE = write_material("gi_blue", base_color=(0.1, 0.2, 0.6, 1.0))
MAT_EM_WARM = write_material("gi_em_warm", base_color=(0.0, 0.0, 0.0, 1.0), emissive=(1.0, 0.85, 0.6, 30.0))

PBR = {s: IDX.material(f"pbr_{s}") for s in
       ("red_brick", "blue_plaster_wall", "wood_floor", "green_metal_rust", "clay_roof_tiles", "checkered_pavement_tiles")}

WALL_T = 0.25
GROUND_TOP = -WALL_T - 0.02
DOWN = (0.7071068, 0.7071068, 0.0, 0.0)   # area light normal (+Z local) points -Y

entities = []
folders = []

def folder(name, parent=0):
    fid = next_id()
    folders.append({SCENE_FOLDER: {"folderId": fid, "name": name, "parentFolder": parent}})
    return fid

def solid_box(name, corner, size, material, folder_id=0, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, corner, rot, folder_id=folder_id)
    fields, idx = box_params(*size)
    add_procedural(e, idx, fields)
    e[PROCEDURAL]["material"] = material
    entities.append(e)
    return e

def light_entity(name, pos, folder_id=0, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot, folder_id=folder_id)
    entities.append(e)
    return e

def label(name, text, pos, folder_id=0):
    e = base_entity(name, pos, (0.0, 0.0, 1.0, 0.0), folder_id=folder_id)   # 180 about Y, faces the -Z aisle
    add_world_text(e, text, ROBOTO_FONT, TEXT_MATERIAL, scale=0.5)
    entities.append(e)
    return e

def open_front_room(tag, cx, cz, inner, mats, folder_id):
    """Open-front box room; mats = {face: material} with 'default' fallback. Interior floor top at y=0."""
    sx, sy, sz = inner
    x0, z0 = cx - sx * 0.5, cz - sz * 0.5
    d = mats.get("default", MAT_WHITE)
    solid_box(f"[{tag}] floor", (x0 - WALL_T, -WALL_T, z0 - WALL_T), (sx + 2 * WALL_T, WALL_T, sz + 2 * WALL_T), mats.get("floor", d), folder_id)
    solid_box(f"[{tag}] ceiling", (x0 - WALL_T, sy, z0 - WALL_T), (sx + 2 * WALL_T, WALL_T, sz + 2 * WALL_T), mats.get("ceiling", d), folder_id)
    solid_box(f"[{tag}] west", (x0 - WALL_T, 0.0, z0 - WALL_T), (WALL_T, sy, sz + 2 * WALL_T), mats.get("west", d), folder_id)
    solid_box(f"[{tag}] east", (x0 + sx, 0.0, z0 - WALL_T), (WALL_T, sy, sz + 2 * WALL_T), mats.get("east", d), folder_id)
    solid_box(f"[{tag}] north", (x0, 0.0, z0 + sz), (sx, sy, WALL_T), mats.get("north", d), folder_id)

def ceiling_panel(tag, cx, cz, y, folder_id, intensity=250.0, half=0.8):
    e = light_entity(f"[{tag}] panel", (cx, y, cz), folder_id, rot=DOWN)
    add_area_light(e, color=(1.0, 1.0, 1.0), intensity=intensity, half_width=half, half_height=half, draw_range=10.0, draw_emissive=True)

# =============================================================================
# Row A: chroma (z=0), interiors 6 x 4.5 x 6
# =============================================================================
A_INNER = (6.0, 4.5, 6.0)

def cornell_blockers(tag, cx, cz, folder_id, material):
    # Tall box rotated 18 degrees about Y (classic layout), short box front-right.
    # Quat computed, not hand-rounded: Jolt asserts IsNormalized at ~1e-5 and 4-decimal constants drift past it
    half = math.radians(18.0) * 0.5
    rot = (math.cos(half), 0.0, math.sin(half), 0.0)
    solid_box(f"[{tag}] tall", (cx - 1.9, 0.0, cz + 0.2), (1.2, 2.4, 1.2), material, folder_id, rot=rot)
    solid_box(f"[{tag}] short", (cx + 0.7, 0.0, cz - 1.4), (1.2, 1.2, 1.2), material, folder_id)

def build_row_a():
    specs = [
        ("A1", "Cornell White - reference", {}),
        ("A2", "Cornell Classic - red west, green east", {"west": MAT_RED, "east": MAT_GREEN}),
        ("A3", "Cornell Saturated - RGB walls, clay floor", {"west": MAT_RED, "east": MAT_GREEN, "north": MAT_BLUE, "floor": PBR["clay_roof_tiles"]}),
    ]
    for i, (tag, text, mats) in enumerate(specs):
        cx = (i - 1.5) * 16.0
        fid = folder(tag, fid_row_a)
        open_front_room(tag, cx, 0.0, A_INNER, mats, fid)
        cornell_blockers(tag, cx, 0.0, fid, MAT_WHITE)
        ceiling_panel(tag, cx, 0.0, A_INNER[1] - 0.05, fid)
        label(f"[{tag}] Label", text, (cx, 5.2, -3.7), fid)

    # A4: emissive-only
    tag, cx = "A4", 1.5 * 16.0
    fid = folder(tag, fid_row_a)
    open_front_room(tag, cx, 0.0, A_INNER, {}, fid)
    cornell_blockers(tag, cx, 0.0, fid, MAT_WHITE)
    for k, dx in enumerate((-1.6, 1.6)):
        solid_box(f"[{tag}] emitter {k}", (cx + dx - 1.0, A_INNER[1] - 0.15, -1.0), (2.0, 0.1, 2.0), MAT_EM_WARM, fid)
    label(f"[{tag}] Label", "Emissive Only - no analytic lights, GI carries everything", (cx, 5.2, -3.7), fid)

# =============================================================================
# Row B: transport (z=20)
# =============================================================================
def build_corridor(fid):
    """L-corridor: visible leg along X (open at -X end... front face is -Z side opening at the leg start),
    hidden leg along +Z at the far end with the only light. Interior 2 wide, 3 high."""
    tag = "B1"
    cx0 = -34.0            # open end (west) of the visible leg
    z0 = 19.0              # visible leg interior z 19..21
    LEN, W, H, LEG = 12.0, 2.0, 3.0, 6.0
    brick, wood = PBR["red_brick"], PBR["wood_floor"]
    # Visible leg: open at west end; hidden leg attaches at the east end, going +Z
    solid_box(f"[{tag}] floor", (cx0 - WALL_T, -WALL_T, z0 - WALL_T), (LEN + 2 * WALL_T, WALL_T, W + LEG + 2 * WALL_T), wood, fid)
    solid_box(f"[{tag}] ceiling", (cx0 - WALL_T, H, z0 - WALL_T), (LEN + 2 * WALL_T, WALL_T, W + LEG + 2 * WALL_T), brick, fid)
    solid_box(f"[{tag}] south", (cx0, 0.0, z0 - WALL_T), (LEN, H, WALL_T), brick, fid)
    # North wall of visible leg stops where the hidden leg opens (last 2m)
    solid_box(f"[{tag}] north", (cx0, 0.0, z0 + W), (LEN - W, H, WALL_T), brick, fid)
    # Hidden leg: z0+W .. z0+W+LEG, x = far 2m of the corridor
    lx0 = cx0 + LEN - W
    solid_box(f"[{tag}] leg west", (lx0 - WALL_T, 0.0, z0 + W), (WALL_T, H, LEG + WALL_T), brick, fid)
    solid_box(f"[{tag}] leg east", (lx0 + W, 0.0, z0 - WALL_T), (WALL_T, H, W + LEG + 2 * WALL_T), brick, fid)
    solid_box(f"[{tag}] leg north", (lx0, 0.0, z0 + W + LEG), (W, H, WALL_T), brick, fid)
    # The only light: at the far end of the hidden leg, facing down the leg (-Z)
    e = light_entity(f"[{tag}] light", (lx0 + W * 0.5, H * 0.6, z0 + W + LEG - 0.15), fid, rot=(0.0, 0.0, 1.0, 0.0))
    add_area_light(e, color=(1.0, 0.95, 0.9), intensity=500.0, half_width=0.8, half_height=1.0, draw_range=14.0, draw_emissive=True)
    label(f"[{tag}] Label", "Bounce Corridor - light hidden around the corner, red brick tints every bounce", (cx0 + LEN * 0.5, 4.0, 18.3), fid)

def build_leak_ladder(fid):
    """Bright sealed room at the back; six dark closets in front share its south wall, which thins
    per closet: 2.0 / 1.0 / 0.5 / 0.25 / 0.10 / 0.05 (first run leaked at all of 0.25/0.10/0.05,
    so the ladder brackets the threshold from above). Closets open to the aisle (-Z); the shared
    wall's BACK face is one plane, so thinner walls mean deeper closets."""
    tag = "B2"
    cx = -9.0
    thick = [2.0, 1.0, 0.5, 0.25, 0.10, 0.05]
    CELL_W, H = 3.0, 3.0
    W6 = CELL_W * len(thick)
    x0 = cx - W6 * 0.5
    z0 = 20.0                # closet fronts (open)
    z_b = 24.5               # shared wall back face = bright room front plane
    ROOM_D = 3.0
    depth = z_b + ROOM_D - z0
    solid_box(f"[{tag}] room floor", (x0 - WALL_T, -WALL_T, z0 - WALL_T), (W6 + 2 * WALL_T, WALL_T, depth + 2 * WALL_T), MAT_WHITE, fid)
    solid_box(f"[{tag}] room ceiling", (x0 - WALL_T, H, z0 - WALL_T), (W6 + 2 * WALL_T, WALL_T, depth + 2 * WALL_T), MAT_WHITE, fid)
    solid_box(f"[{tag}] room west", (x0 - WALL_T, 0.0, z0 - WALL_T), (WALL_T, H, depth + 2 * WALL_T), MAT_WHITE, fid)
    solid_box(f"[{tag}] room east", (x0 + W6, 0.0, z0 - WALL_T), (WALL_T, H, depth + 2 * WALL_T), MAT_WHITE, fid)
    solid_box(f"[{tag}] room north", (x0, 0.0, z_b + ROOM_D), (W6, H, WALL_T), MAT_WHITE, fid)
    for i, t in enumerate(thick):
        sx0 = x0 + i * CELL_W
        solid_box(f"[{tag}] shared {t}", (sx0, 0.0, z_b - t), (CELL_W, H, t), MAT_WHITE, fid)
        if i > 0:
            solid_box(f"[{tag}] divider {i}", (sx0 - WALL_T * 0.5, 0.0, z0), (WALL_T, H, z_b - z0), MAT_WHITE, fid)
        label(f"[{tag}] t{t}", f"{t:.2f}", (sx0 + CELL_W * 0.5, 3.4, z0 - 0.4), fid)
    e = light_entity(f"[{tag}] blast", (cx, H - 0.2, z_b + ROOM_D * 0.5), fid, rot=DOWN)
    add_area_light(e, color=(1.0, 1.0, 1.0), intensity=1200.0, half_width=7.0, half_height=1.2, draw_range=9.0, draw_emissive=True)
    label(f"[{tag}] Label", "Leak Ladder - sealed bright room behind; shared wall 2.0 down to 0.05, any light in a closet is a leak", (cx, 4.2, 19.3), fid)

def build_pillar_court(fid):
    tag = "B3"
    cx, cz = 8.0, 24.0
    INNER = (8.0, 4.0, 8.0)
    open_front_room(tag, cx, cz, INNER, {"default": MAT_WHITE, "floor": PBR["checkered_pavement_tiles"]}, fid)
    for ix in range(4):
        for iz in range(4):
            px = cx - 3.0 + ix * 2.0
            pz = cz - 3.0 + iz * 2.0
            e = base_entity(f"[{tag}] pillar {ix}{iz}", (px, 1.75, pz), folder_id=fid)
            fields, idx = cylinder_params(0.22, 3.5, slices=20)
            add_procedural(e, idx, fields)
            e[PROCEDURAL]["material"] = MAT_WHITE
            entities.append(e)
    for k, dx in enumerate((-2.0, 2.0)):
        e = base_entity(f"[{tag}] mast {k}", (cx + dx - 0.25, 0.0, cz + 1.0), folder_id=fid)
        fields, idx = lattice_params(0.5, 3.8, 0.5, chord=0.06, brace=0.04, bays=4)
        add_procedural(e, idx, fields)
        e[PROCEDURAL]["material"] = PBR["green_metal_rust"]
        entities.append(e)
    ceiling_panel(tag, cx, cz, INNER[1] - 0.05, fid, intensity=320.0, half=0.6)
    label(f"[{tag}] Label", "Pillar Court - nearfield occlusion variance (arcade failure shape)", (cx, 5.0, 19.3), fid)

def build_material_court(fid):
    tag = "B4"
    cx, cz = 26.0, 24.0
    INNER = (10.0, 4.0, 10.0)
    open_front_room(tag, cx, cz, INNER, {"default": MAT_WHITE, "west": PBR["red_brick"], "east": PBR["blue_plaster_wall"], "north": PBR["wood_floor"]}, fid)
    # Quartered floor overlay (2cm above the structural floor)
    quads = [("wood_floor", -1, -1), ("checkered_pavement_tiles", 0, -1), ("clay_roof_tiles", -1, 0), ("green_metal_rust", 0, 0)]
    for mat, qx, qz in quads:
        solid_box(f"[{tag}] floor {mat}", (cx + qx * 5.0, 0.0, cz + qz * 5.0), (5.0, 0.02, 5.0), PBR[mat], fid)
    # Spheres and boxes wearing the sets
    shapes = [("red_brick", -3.0, -2.5), ("blue_plaster_wall", 0.0, -2.0), ("clay_roof_tiles", 3.0, -2.5)]
    for i, (mat, dx, dz) in enumerate(shapes):
        e = base_entity(f"[{tag}] sphere {i}", (cx + dx, 0.82, cz + dz), folder_id=fid)
        fields, idx = sphere_params(0.8, slices=32, stacks=16)
        add_procedural(e, idx, fields)
        e[PROCEDURAL]["material"] = PBR[mat]
        entities.append(e)
    for i, (mat, dx, dz) in enumerate([("green_metal_rust", -2.0, 1.5), ("checkered_pavement_tiles", 1.5, 2.0)]):
        solid_box(f"[{tag}] box {i}", (cx + dx, 0.02, cz + dz), (1.4, 1.4, 1.4), PBR[mat], fid)
    e = light_entity(f"[{tag}] key", (cx - 2.0, INNER[1] - 0.05, cz), fid, rot=DOWN)
    add_area_light(e, color=(1.0, 0.9, 0.75), intensity=300.0, half_width=1.0, half_height=1.0, draw_range=12.0, draw_emissive=True)
    e = light_entity(f"[{tag}] fill", (cx + 3.5, INNER[1] - 0.05, cz + 3.5), fid, rot=DOWN)
    add_area_light(e, color=(0.7, 0.8, 1.0), intensity=120.0, half_width=0.6, half_height=0.6, draw_range=10.0, draw_emissive=True)
    label(f"[{tag}] Label", "Material Court - all six PBR sets, warm key + cool fill", (cx, 5.0, 18.3), fid)

# =============================================================================
# build
# =============================================================================
fid_row_a = folder("Row A - Chroma")
fid_row_b = folder("Row B - Transport")

build_row_a()
build_corridor(folder("B1", fid_row_b))
build_leak_ladder(folder("B2", fid_row_b))
build_pillar_court(folder("B3", fid_row_b))
build_material_court(folder("B4", fid_row_b))

solid_box("Ground", (-45.0, GROUND_TOP - 0.5, -12.0), (85.0, 0.5, 45.0), MAT_GREY)

sky = base_entity("Skybox", (0.0, 0.0, 0.0))
add_skybox(sky, ENV_MAP, intensity=0.0, priority=0)
entities.append(sky)

entities.extend(folders)

editor_camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 4.0, 14.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "GI Stress", editor_camera=editor_camera)

n_lights = sum(1 for e in entities if wa.LIGHT_AREA in e)
print(f"wrote {SCENE_PATH} ({len(entities)} entities, {n_lights} area lights)")
