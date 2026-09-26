#!/usr/bin/env python3
"""
Builds "Prism Run": a Ballance course that descends through five lighting zones.

  I   SUNRISE TERRACE  sun + sky, pergola shadow stripes, pillar slalom, narrow bridge
  II  PRISM HALL       sun through windows/skylights only, coloured bounce, sliding pushers
  III NEON GALLERY     emissive strips as lights, glossy catwalk with gaps over a mirror pool,
                       spinning sweeper under an area-light softbox
  IV  LANTERN CAVERN   two-turn spiral ramp around an emissive-banded column, hanging sphere
                       lanterns, three coloured lights orbiting on path movers
  V   GOLDEN FINISH    back into sunlight, ferry platform, gold arch, crates to smash

Everything sits over a reflective sea. Track pieces are axis-aligned pad -> segment -> pad
chains; segment ends are derived from pad edges so joins are exact, and verify() asserts it.
Run from repo root:  python scripts/build_prism_run.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("prism_run")

from wscene_authoring import (
    base_entity, box_params, cylinder_params, sphere_params, torus_params, icosa_params, plane_params,
    spiral_params, ring_params, oriented_box, add_procedural, name_id,
    PROCEDURAL, PHYSICS, TEXT3D, SCENE_FOLDER, SPAWN, CHECKPOINT, DEATH_ZONE,
    add_directional_light, add_sphere_light, add_area_light, add_skybox, add_path_mover,
    add_reflection_probe, add_render_flags, add_checkpoint,
    PROBE_RES_128, EASE_IN_OUT_SINE, EASE_LINEAR, LOOP_PINGPONG, LOOP_LOOP, ALIGN_CENTER, ANCHOR_CENTER,
)
import asset_index

IDX = asset_index.scan()
ROBOTO_FONT = IDX.font("Roboto")
SKY = IDX.envmap("kloofendal_48d_partly_cloudy_puresky_4k")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_NAME = "Prism Run"
SCENE_PATH = os.path.join(REPO, "scenes", "prism_run.wscene")
SCENE_ID = name_id("prism_run")
ROTATE = wa.component_key("RotateInPlaceComponent")

K = 65536.0          # physical light unit scale (legacy strength 1 = 65536 nits)
TH = 0.5             # track / pad slab thickness
WALL_H = 0.8         # rail height, 0.1 embedded -> top 0.7 above the surface
WALL_T = 0.25
SUN_DIR = (-0.70, -0.62, 0.35)   # direction light travels: from the south-east, ~38 deg up

# =============================================================================
# materials
# =============================================================================
def tex_set(name, packed):
    diff = wa.write_texture_stub(f"{name}_diff", f"src/{name}/{name}_diff_1k.png", wa.DXGI_BC7_UNORM_SRGB)
    nor = wa.write_texture_stub(f"{name}_nor", f"src/{name}/{name}_nor_1k.png", wa.DXGI_BC5_UNORM)
    mr_name = "arm" if packed else "rough"
    mr = wa.write_texture_stub(f"{name}_{mr_name}", f"src/{name}/{name}_{mr_name}_1k.png", wa.DXGI_BC7_UNORM)
    return diff, nor, mr

T_TILES = tex_set("checkered_pavement_tiles", False)
T_CONCRETE = tex_set("concrete", True)
T_WOOD = tex_set("wood_floor", False)
T_BRICK = tex_set("red_brick", False)
T_PLATE = tex_set("metal_plate", True)
T_GRAVEL = tex_set("gravelly_sand", True)

def textured(name, tex, uv=0.5, tint=(1.0, 1.0, 1.0), metallic=0.0, roughness=1.0):
    diff, nor, mr = tex
    return wa.write_material(name, base_color=(*tint, 1.0), metallic=metallic, roughness=roughness,
                             albedo_tex=diff, metal_rough_tex=mr, normal_tex=nor, uv_scale=(uv, uv))

def flat(name, rgb, roughness=0.9, metallic=0.0):
    return wa.write_material(name, base_color=(*rgb, 1.0), metallic=metallic, roughness=roughness)

def glow(name, rgb, strength):
    return wa.write_material(name, base_color=(0.0, 0.0, 0.0, 1.0), emissive=(*rgb, strength * K), lighting_shader="default_pbr")

M_TRACK = textured("prism_track_tiles", T_TILES, uv=0.5, tint=(1.05, 1.0, 0.95))
M_STONE = textured("prism_stone", T_CONCRETE, uv=0.25, tint=(1.1, 1.05, 0.98))
M_WOOD = textured("prism_wood", T_WOOD, uv=0.5, tint=(0.9, 0.75, 0.6))
M_HALL_WHITE = flat("prism_hall_white", (0.80, 0.78, 0.74))
M_HALL_FLOOR = textured("prism_hall_floor", T_CONCRETE, uv=0.25, tint=(1.3, 1.28, 1.22))
M_CRIMSON = flat("prism_crimson", (0.78, 0.05, 0.04), 0.85)
M_COBALT = flat("prism_cobalt", (0.05, 0.14, 0.75), 0.85)
M_SAFFRON = flat("prism_saffron", (0.92, 0.62, 0.10), 0.7)
M_PUSHER = flat("prism_pusher", (0.95, 0.30, 0.06), 0.35)
M_GLOSS = flat("prism_gloss_black", (0.02, 0.02, 0.025), 0.06)
M_MIRROR = flat("prism_mirror", (0.95, 0.95, 0.97), 0.03, metallic=1.0)
M_TUNNEL = textured("prism_tunnel_plate", T_PLATE, uv=0.5, tint=(0.25, 0.25, 0.28), metallic=1.0, roughness=0.8)
M_NEON_MAGENTA = glow("prism_neon_magenta", (1.0, 0.08, 0.75), 40.0)
M_NEON_CYAN = glow("prism_neon_cyan", (0.08, 0.8, 1.0), 40.0)
M_BRICK = textured("prism_brick", T_BRICK, uv=0.5, tint=(0.75, 0.7, 0.68))
M_GRAVEL = textured("prism_gravel", T_GRAVEL, uv=0.25, tint=(0.5, 0.48, 0.45))
M_COLUMN = textured("prism_column", T_BRICK, uv=1.0, tint=(0.45, 0.4, 0.38))
M_BAND = glow("prism_band_amber", (1.0, 0.55, 0.18), 20.0)
M_CHAIN = flat("prism_chain", (0.2, 0.2, 0.2), 0.5, metallic=1.0)
M_SPIRAL = textured("prism_spiral", T_CONCRETE, uv=0.5, tint=(0.95, 0.9, 0.85))
M_GOLD = flat("prism_gold", (1.0, 0.78, 0.35), 0.18, metallic=1.0)
M_CHROME = flat("prism_chrome", (0.95, 0.95, 0.95), 0.05, metallic=1.0)
M_SEA = flat("prism_sea", (0.02, 0.06, 0.09), 0.12)
M_CHECKPOINT = glow("prism_checkpoint", (0.2, 1.0, 0.35), 1.5)
M_FINISH = glow("prism_finish_text", (1.0, 0.72, 0.3), 12.0)
M_TITLE = flat("prism_title", (0.92, 0.9, 0.86), 0.5)
M_CRATES = [flat(f"prism_crate_{i}", c, 0.3) for i, c in enumerate(
    [(0.85, 0.1, 0.08), (0.1, 0.35, 0.9), (0.95, 0.75, 0.1), (0.1, 0.7, 0.3), (0.9, 0.9, 0.9)])]

# =============================================================================
# entity helpers
# =============================================================================
entities = []
_folders = {}

def folder(name):
    fid = name_id(f"prism_folder_{name}")
    if fid not in _folders:
        _folders[fid] = name
        entities.append({SCENE_FOLDER: {"folderId": fid, "name": name, "parentFolder": 0}})
    return fid

CUR = [0]

def ent(name, pos, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot, folder_id=CUR[0])
    entities.append(e)
    return e

def mesh(e, shape, material):
    fields, idx = shape
    e[PROCEDURAL] = {**fields, "material": material, "renderOffset": [0.0, 0.0, 0.0],
                     "renderRotation": [1.0, 0.0, 0.0, 0.0], "type": idx}
    return e

def body(e, shapes, motion=0, sensor=False, mass=1.0, friction=0.5, restitution=0.0):
    e[PHYSICS] = {"motionType": motion, "mass": mass, "friction": friction, "restitution": restitution,
                  "motionQuality": 1 if motion == 2 else 0, "layerOverride": 65535,
                  "enhancedInternalEdgeRemoval": False, "isSensor": sensor, "shapes": shapes}
    return e

def box_shape(size):
    h = [size[0] * 0.5, size[1] * 0.5, size[2] * 0.5]
    return {"type": 0, "halfExtents": h, "offset": h, "rotation": [1.0, 0.0, 0.0, 0.0],
            "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}

def sphere_shape(radius):
    return {"type": 1, "radius": radius, "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0],
            "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}

AABBS = []   # (name, lo, hi) of every static axis-aligned render box, for the z-fight check

def slab(name, lo, hi, material, physics=True, motion=0):
    """Axis-aligned box from world min corner lo to max corner hi."""
    size = (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
    assert min(size) > 0.0, f"{name}: degenerate slab {size}"
    if motion == 0:
        AABBS.append((name, tuple(lo), tuple(hi)))
    e = ent(name, lo)
    mesh(e, box_params(*size), material)
    if physics and min(size) >= 0.12:
        body(e, [box_shape(size)], motion=motion)
    return e

def proc(name, pos, shape, material, rot=(1.0, 0.0, 0.0, 0.0), physics=True, motion=0):
    """Procedural mesh with the matching generated collider (works under any rotation)."""
    fields, idx = shape
    e = ent(name, pos, rot)
    add_procedural(e, idx, fields, motion=motion)
    e[PROCEDURAL]["material"] = material
    if not physics:
        del e[PHYSICS]
    return e

def sign(name, pos, text, rot, scale=1.0, material=M_TITLE, depth=0.15):
    e = ent(name, pos, rot)
    e[TEXT3D] = {"depth": depth, "flatness": 0.0005, "fontId": ROBOTO_FONT, "material": material,
                 "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0],
                 "scale": scale, "smoothNormals": True, "text": text, "tracking": 0.08,
                 "align": ALIGN_CENTER, "anchor": ANCHOR_CENTER, "wrapWidth": 0.0, "bendRadius": 0.0}
    return e

def face_dir(dx, dy, dz):
    """[w,x,y,z] mapping local +Z onto a world direction (sun direction, area-light normal)."""
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / n, dy / n, dz / n
    if dz < -0.999999:
        return [0.0, 0.0, 1.0, 0.0]
    w = 1.0 + dz
    n = math.sqrt(w * w + dy * dy + dx * dx)
    return [w / n, -dy / n, dx / n, 0.0]

def yaw_quat(deg):
    h = math.radians(deg) * 0.5
    return (math.cos(h), 0.0, math.sin(h), 0.0)

FACE_NEG_Z = yaw_quat(180.0)   # Text3D glyph face -> -Z, read by a camera looking +Z
FACE_NEG_X = yaw_quat(-90.0)   # glyph face -> -X, read by a camera looking +X

def emissive_light(e):
    add_render_flags(e, emissive_light=True)
    return e

def death_zone(name, lo, hi):
    size = (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
    e = ent(name, lo)
    body(e, [box_shape(size)], sensor=True)
    e[DEATH_ZONE] = {}
    return e

# =============================================================================
# course: pads + axis-aligned segments between them
# =============================================================================
PADS = {}
SURFACES = []   # (name, kind, data) for verify()

def pad(name, center, size, material=M_TRACK, walls="NSEW", pillar=False, depth=TH):
    cx, y, cz = center
    sx, sz = size
    PADS[name] = {"c": center, "s": size, "walls": walls, "links": {}}
    slab(f"{name}", (cx - sx / 2, y - depth, cz - sz / 2), (cx + sx / 2, y, cz + sz / 2), material)
    SURFACES.append((name, "pad", (cx - sx / 2, cx + sx / 2, cz - sz / 2, cz + sz / 2, y)))
    if pillar:
        r = min(sx, sz) * 0.3
        top, bottom = y - depth, -1.0
        proc(f"{name} Pillar", (cx, (top + bottom) * 0.5, cz), cylinder_params(r, top - bottom, 24), M_STONE, physics=False)
    return PADS[name]

def _axis(a, b):
    (ax, _, az), (bx, _, bz) = a["c"], b["c"]
    if abs(ax - bx) > 1e-6 and abs(az - bz) > 1e-6:
        raise ValueError("segment between pads must be axis-aligned")
    if abs(ax - bx) > 1e-6:
        return (1.0 if bx > ax else -1.0, 0.0)
    return (0.0, 1.0 if bz > az else -1.0)

def _side(d):
    return {(1.0, 0.0): "E", (-1.0, 0.0): "W", (0.0, 1.0): "N", (0.0, -1.0): "S"}[d]

def edge_point(p, d):
    cx, y, cz = p["c"]
    sx, sz = p["s"]
    return (cx + d[0] * sx / 2, y, cz + d[1] * sz / 2)

SEAT = 0.3   # physics-only reach under the neighbouring surface, so the ball never meets two rounded collider edges at a join

def seats(start, end):
    """Flat segments seat into both pads; sloped ones only at the low end (the high end would stand proud)."""
    if abs(start[1] - end[1]) < 1e-6:
        return SEAT, SEAT
    return (SEAT, 0.0) if start[1] < end[1] else (0.0, SEAT)

def ramp(name, start, end, width, material, walls=True, wall_mat=None, seat=(0.0, 0.0)):
    corner, quat, length = oriented_box(start, end, width, TH)
    e = ent(name, corner, quat)
    mesh(e, box_params(width, TH, length), material)
    s0, s1 = seat
    half = [width / 2, TH / 2, (length + s0 + s1) / 2]
    body(e, [{"type": 0, "halfExtents": half, "offset": [width / 2, TH / 2, (length + s1 - s0) / 2],
              "rotation": [1.0, 0.0, 0.0, 0.0], "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}])
    SURFACES.append((name, "ramp", (start, end, width)))
    if abs(start[1] - end[1]) < 1e-6:
        y = start[1]
        along = 0 if abs(start[0] - end[0]) > 1e-6 else 2
        lat = 2 - along
        a0, a1 = sorted((start[along], end[along]))
        def aabb(tag, l0, l1, y0, y1):
            lo, hi = [0.0] * 3, [0.0] * 3
            lo[along], hi[along], lo[lat], hi[lat], lo[1], hi[1] = a0, a1, l0, l1, y0, y1
            AABBS.append((tag, tuple(lo), tuple(hi)))
        mid = start[lat]
        aabb(name, mid - width / 2, mid + width / 2, y - TH, y)
        if walls:
            aabb(f"{name} Rail L", mid - width / 2 - WALL_T, mid - width / 2, y - 0.1, y - 0.1 + WALL_H)
            aabb(f"{name} Rail R", mid + width / 2, mid + width / 2 + WALL_T, y - 0.1, y - 0.1 + WALL_H)
    if walls:
        for side, sgn in (("L", -1.0), ("R", 1.0)):
            c2, q2, l2 = oriented_box(start, end, WALL_T, WALL_H, lateral_offset=sgn * (width + WALL_T) / 2,
                                      vertical_offset=WALL_H - 0.1)
            w = ent(f"{name} Rail {side}", c2, q2)
            f2, i2 = box_params(WALL_T, WALL_H, l2)
            add_procedural(w, i2, f2)
            w[PROCEDURAL]["material"] = wall_mat or M_STONE
    return e

def link(a_name, b_name, width, material=M_TRACK, walls=True, gap=False):
    """Segment from pad a's edge to pad b's edge. gap=True leaves the span empty (ferry / jump)."""
    a, b = PADS[a_name], PADS[b_name]
    d = _axis(a, b)
    a["links"][_side(d)] = (width, walls)
    b["links"][_side((-d[0], -d[1]))] = (width, walls)
    if gap:
        return None
    start, end = edge_point(a, d), edge_point(b, (-d[0], -d[1]))
    return ramp(f"{a_name}->{b_name}", start, end, width, material, walls, seat=seats(start, end))

def pad_walls(wall_mat=M_STONE):
    """Rails round every pad edge, cut open where a segment attaches (stubs meet that segment's rails)."""
    for name, p in PADS.items():
        cx, y, cz = p["c"]
        sx, sz = p["s"]
        y0, y1 = y - 0.1, y - 0.1 + WALL_H
        t = WALL_T
        for side in p["walls"]:
            horizontal = side in "NS"
            if horizontal:
                z0 = cz + sz / 2 if side == "N" else cz - sz / 2 - t
                lo_run, hi_run, mid = cx - sx / 2 - t, cx + sx / 2 + t, cx
                # a segment as wide as the pad runs its own rails through this corner
                if "W" in p["links"] and p["links"]["W"][0] / 2 >= sz / 2 - 1e-6:
                    lo_run = cx - sx / 2
                if "E" in p["links"] and p["links"]["E"][0] / 2 >= sz / 2 - 1e-6:
                    hi_run = cx + sx / 2
            else:
                x0 = cx + sx / 2 if side == "E" else cx - sx / 2 - t
                lo_run, hi_run, mid = cz - sz / 2, cz + sz / 2, cz
            spans = [(lo_run, hi_run)]
            if side in p["links"]:
                width, seg_walls = p["links"][side]
                inset = t if seg_walls else 0.0
                spans = [(lo_run, mid - width / 2 - inset), (mid + width / 2 + inset, hi_run)]
            for i, (r0, r1) in enumerate(spans):
                if r1 - r0 < 0.05:
                    continue
                if horizontal:
                    slab(f"{name} Rail {side}{i}", (r0, y0, z0), (r1, y1, z0 + t), wall_mat)
                else:
                    slab(f"{name} Rail {side}{i}", (x0, y0, r0), (x0 + t, y1, r1), wall_mat)

CHECKPOINTS = []

def checkpoint(pad_name, priority, label):
    p = PADS[pad_name]
    cx, y, cz = p["c"]
    sx, sz = p["s"]
    e = ent(f"Checkpoint {priority} {label}", (cx - sx / 2, y, cz - sz / 2))
    body(e, [box_shape((sx, 3.0, sz))], sensor=True)
    add_checkpoint(e, name_id(f"prism_cp_{label}"), priority=priority, spawn_offset=(sx / 2, 1.0, sz / 2))
    disc = ent(f"Checkpoint {priority} Disc", (cx, y + 0.02, cz))
    mesh(disc, ring_params(1.3, 1.0, 48, False), M_CHECKPOINT)
    emissive_light(disc)
    CHECKPOINTS.append((priority, label, (cx, y + 1.0, cz)))
    return e

# =============================================================================
# world: sun, sky, sea
# =============================================================================
CUR[0] = folder("World")
sun = ent("Sun", (0.0, 60.0, 0.0), face_dir(*SUN_DIR))
add_directional_light(sun, color=(1.0, 0.93, 0.82), intensity=100000.0, priority=10, angular_radius_deg=0.5)
add_skybox(sun, SKY, intensity=30000.0, priority=10)

sea = ent("Sea", (40.0, 0.0, 80.0))
mesh(sea, plane_params(700.0, 700.0, 1, 1), M_SEA)
death_zone("Sea Death Zone", (-300.0, -4.0, -300.0), (400.0, 1.5, 450.0))

# =============================================================================
# I  SUNRISE TERRACE
# =============================================================================
CUR[0] = folder("I Sunrise Terrace")
pad("P0 Start", (0.0, 40.0, 0.0), (10.0, 10.0), pillar=True)
pad("P1 Corner", (0.0, 38.0, 22.0), (6.0, 6.0), pillar=True)
pad("P2 Corner", (24.0, 38.0, 22.0), (6.0, 6.0), pillar=True)
pad("P3 Gate", (24.0, 36.0, 44.0), (8.0, 8.0), walls="SEW", pillar=True)
link("P0 Start", "P1 Corner", 4.0)
link("P1 Corner", "P2 Corner", 6.0)
link("P2 Corner", "P3 Gate", 1.6, walls=False)

spawn = ent("Spawn", (0.0, 41.0, 0.0))
spawn[SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 1}
checkpoint("P0 Start", 1, "start")
sign("Title", (0.0, 44.6, 5.2), "PRISM RUN", FACE_NEG_Z, scale=1.4)

# pergola over the first descent: posts outside the rails, beams across, stripes of sun on the track
for i in range(7):
    z = 6.0 + i * 2.2
    y = 40.0 - 2.0 * (z - 5.0) / 14.0
    for x in (-2.9, 2.9):
        proc(f"Pergola Post {i}{'W' if x < 0 else 'E'}", (x, y + 1.5, z), cylinder_params(0.14, 4.0, 12), M_WOOD, physics=False)
    slab(f"Pergola Beam {i}", (-3.4, y + 3.3, z - 0.12), (3.4, y + 3.6, z + 0.12), M_WOOD, physics=False)
for x in (-2.9, 2.9):
    corner, quat, length = oriented_box((x, 40.0 + 3.75, 5.0), (x, 38.0 + 3.75, 19.0), 0.25, 0.25)
    e = ent(f"Pergola Rail {'W' if x < 0 else 'E'}", corner, quat)
    mesh(e, box_params(0.25, 0.25, length), M_WOOD)
for i in range(18):
    z = 5.4 + i * 0.75
    y = 40.0 - 2.0 * (z - 5.0) / 14.0
    slab(f"Pergola Lath {i}", (-3.1, y + 3.88, z - 0.05), (3.1, y + 3.98, z + 0.05), M_WOOD, physics=False)

# slalom: staggered pillars on the wide flat run
for i, (x, dz) in enumerate(((7.0, 1.3), (11.0, -1.3), (15.0, 1.3), (19.0, -1.3))):
    proc(f"Slalom Pillar {i}", (x, 38.0 + 1.5, 22.0 + dz), cylinder_params(0.45, 3.0, 24), M_STONE)
    proc(f"Slalom Cap {i}", (x, 38.0 + 3.1, 22.0 + dz), sphere_params(0.55, 24, 16), M_GOLD, physics=False)

sign("Sign II", (24.0, 39.2, 49.2), "II  PRISM HALL", FACE_NEG_Z, scale=0.55)

# =============================================================================
# II  PRISM HALL   interior x 6..42, z 50..82, y 24..40
# =============================================================================
CUR[0] = folder("II Prism Hall")
HX0, HX1, HZ0, HZ1, HY0, HY1 = 6.0, 42.0, 50.0, 82.0, 24.0, 40.0
T = 0.5

slab("Hall Foundation", (HX0 - T, -1.0, HZ0 - T), (HX1 + T, HY0 - T, HZ1 + T), M_STONE, physics=False)
slab("Hall Floor", (HX0 - T, HY0 - T, HZ0 - T), (HX1 + T, HY0, HZ1 + T), M_HALL_FLOOR)
slab("Hall West Wall", (HX0 - T, HY0, HZ0 - T), (HX0, HY1, HZ1 + T), M_CRIMSON)
# south wall, door x 21..27 up to 39.5
slab("Hall South Wall A", (HX0, HY0, HZ0 - T), (21.0, HY1, HZ0), M_HALL_WHITE)
slab("Hall South Wall B", (27.0, HY0, HZ0 - T), (HX1, HY1, HZ0), M_HALL_WHITE)
slab("Hall South Wall Sill", (21.0, HY0, HZ0 - T), (27.0, 35.0, HZ0), M_HALL_WHITE)
slab("Hall South Wall Lintel", (21.0, 38.4, HZ0 - T), (27.0, HY1, HZ0), M_HALL_WHITE)
# north wall, door x 35..41, y 28.4..33 into the tunnel
slab("Hall North Wall A", (HX0, HY0, HZ1), (35.0, HY1, HZ1 + T), M_COBALT)
slab("Hall North Wall B", (41.0, HY0, HZ1), (HX1, HY1, HZ1 + T), M_COBALT)
slab("Hall North Wall Sill", (35.0, HY0, HZ1), (41.0, 28.4, HZ1 + T), M_COBALT)
slab("Hall North Wall Lintel", (35.0, 33.0, HZ1), (41.0, HY1, HZ1 + T), M_COBALT)
# east wall: four tall windows facing the sun
WIN = [(53.0, 55.5), (60.0, 62.5), (67.0, 69.5), (74.0, 76.5)]
slab("Hall East Sill", (HX1, HY0, HZ0 - T), (HX1 + T, 29.0, HZ1 + T), M_HALL_WHITE)
slab("Hall East Head", (HX1, 37.0, HZ0 - T), (HX1 + T, HY1, HZ1 + T), M_HALL_WHITE)
edges = [HZ0 - T] + [v for w in WIN for v in w] + [HZ1 + T]
for i in range(0, len(edges), 2):
    slab(f"Hall East Pier {i // 2}", (HX1, 29.0, edges[i]), (HX1 + T, 37.0, edges[i + 1]), M_HALL_WHITE)
# roof with two skylight slots
SKY_SLOTS = [(58.0, 59.5), (70.0, 71.5)]
edges = [HZ0 - T] + [v for s in SKY_SLOTS for v in s] + [HZ1 + T]
for i in range(0, len(edges), 2):
    slab(f"Hall Roof {i // 2}", (HX0 - T, HY1, edges[i]), (HX1 + T, HY1 + T, edges[i + 1]), M_HALL_WHITE)

pad("H0 Entry", (24.0, 35.6, 54.0), (6.0, 6.0), M_SAFFRON, walls="ENW")
pad("H1 Corner", (10.0, 34.0, 54.0), (5.0, 5.0), M_SAFFRON)
pad("H2 Corner", (10.0, 32.7, 66.0), (5.0, 5.0), M_SAFFRON)
pad("H3 Corner", (38.0, 30.9, 66.0), (5.0, 5.0), M_SAFFRON)
pad("H4 Exit", (38.0, 29.6, 78.0), (5.0, 5.0), M_SAFFRON)
link("P3 Gate", "H0 Entry", 4.0)
link("H0 Entry", "H1 Corner", 3.0, M_HALL_WHITE)
link("H1 Corner", "H2 Corner", 3.0, M_HALL_WHITE)
link("H2 Corner", "H3 Corner", 3.0, M_HALL_WHITE, walls=False)
link("H3 Corner", "H4 Exit", 3.0, M_HALL_WHITE)
checkpoint("P3 Gate", 2, "gate")
checkpoint("H2 Corner", 3, "hall")

# pushers sweep across the unwalled alley (z 64.5..67.5); they overshoot 2 m each side
for i, (x, phase) in enumerate(((17.0, 0), (23.5, 1), (30.0, 0))):
    y = 32.7 + (30.9 - 32.7) * (x - 12.5) / (35.5 - 12.5)
    z_from, z_to = (62.3, 68.5) if phase == 0 else (68.5, 62.3)
    e = slab(f"Pusher {i}", (x - 0.6, y + 0.08, z_from), (x + 0.6, y + 1.08, z_from + 1.2), M_PUSHER, motion=1)
    add_path_mover(e, [(0.0, 0.0, 0.0), (0.0, 0.0, z_to - z_from)], speed=0.35, wait_time=0.8,
                   easing=EASE_IN_OUT_SINE, loop_mode=LOOP_PINGPONG, mode=0)

# slowly turning gold icosahedron hanging in the light
ico = proc("Hall Icosahedron", (24.0, 28.5, 74.0), icosa_params(2.2), M_GOLD, physics=False)
ico[ROTATE] = {"axis": [0.3, 1.0, 0.1], "speedDegrees": 12.0, "bWorldSpace": True}
proc("Hall Icosahedron Chain", (24.0, 34.6, 74.0), cylinder_params(0.04, 10.8, 8), M_CHAIN, physics=False)

death_zone("Hall Floor Death Zone", (HX0, HY0 - 0.1, HZ0), (HX1, HY0 + 0.5, HZ1))
e = ent("Hall Probe", ((HX0 + HX1) / 2, (HY0 + HY1) / 2, (HZ0 + HZ1) / 2))
e[wa.TRANSFORM]["scale"] = [(HX1 - HX0) / 2 + T, (HY1 - HY0) / 2 + T, (HZ1 - HZ0) / 2 + T]
add_reflection_probe(e, name_id("prism_probe_hall"), capture_offset=(0.0, 4.0, 0.0), resolution=PROBE_RES_128)

# =============================================================================
# III NEON GALLERY   interior x 34..42, z 82.5..125, y 24..33
# =============================================================================
CUR[0] = folder("III Neon Gallery")
TX0, TX1, TZ0, TZ1, TY0, TY1 = 34.0, 42.0, HZ1 + T, 125.0, 24.0, 33.0
slab("Tunnel Foundation", (TX0 - T, -1.0, TZ0), (TX1 + T, TY0 - T, TZ1), M_STONE, physics=False)
slab("Tunnel Mirror Pool", (TX0 - T, TY0 - T, TZ0), (TX1 + T, TY0, TZ1), M_MIRROR)
slab("Tunnel West Wall", (TX0 - T, TY0, TZ0), (TX0, TY1, TZ1), M_TUNNEL)
slab("Tunnel East Wall", (TX1, TY0, TZ0), (TX1 + T, TY1, TZ1), M_TUNNEL)
slab("Tunnel Ceiling", (TX0 - T, TY1, TZ0), (TX1 + T, TY1 + T, TZ1), M_TUNNEL)
for side, x0, mat_hi, mat_lo in (("W", TX0, M_NEON_MAGENTA, M_NEON_CYAN), ("E", TX1 - 0.12, M_NEON_CYAN, M_NEON_MAGENTA)):
    for tag, y, mat in (("High", 31.2, mat_hi), ("Low", 26.2, mat_lo)):
        emissive_light(slab(f"Neon {side} {tag}", (x0, y, TZ0 + 0.5), (x0 + 0.12, y + 0.12, TZ1 - 0.5), mat, physics=False))
    for i in range(7):
        z = TZ0 + 3.0 + i * 6.0
        mat = mat_hi if i % 2 == 0 else mat_lo
        emissive_light(slab(f"Neon {side} Rib {i}", (x0, 26.5, z), (x0 + 0.12, 31.0, z + 0.12), mat, physics=False))

pad("T0 Landing", (38.0, 29.0, 88.0), (5.0, 5.0), M_GLOSS, walls="EW")
link("H4 Exit", "T0 Landing", 3.0, M_HALL_WHITE)
checkpoint("T0 Landing", 4, "neon")
# catwalk with two gaps, then the sweeper disc
ramp("Catwalk A", (38.0, 29.0, 90.5), (38.0, 28.7, 97.0), 3.0, M_GLOSS, walls=False)
ramp("Catwalk B", (38.0, 28.4, 98.4), (38.0, 28.2, 104.5), 3.0, M_GLOSS, walls=False)
DISC_C, DISC_R, DISC_Y = (38.0, 110.0), 4.0, 27.9
proc("Sweeper Disc", (DISC_C[0], DISC_Y - 0.25, DISC_C[1]), cylinder_params(DISC_R, 0.5, 48), M_GLOSS)
SURFACES.append(("Sweeper Disc", "disc", (DISC_C[0], DISC_C[1], DISC_R, DISC_Y)))
proc("Sweeper Hub", (DISC_C[0], DISC_Y + 0.6, DISC_C[1]), cylinder_params(0.5, 1.2, 24), M_CHROME)
bar = proc("Sweeper Bar", (DISC_C[0], DISC_Y + 0.55, DISC_C[1]), cylinder_params(0.3, 7.4, 16), M_PUSHER,
           rot=(math.cos(math.pi / 4), math.sin(math.pi / 4), 0.0, 0.0), motion=1)
bar[ROTATE] = {"axis": [0.0, 1.0, 0.0], "speedDegrees": 55.0, "bWorldSpace": True}
pad("T3 Exit", (38.0, 27.2, 122.0), (5.0, 5.0), M_GLOSS, walls="EW")
ramp("Catwalk C", (38.0, DISC_Y, DISC_C[1] + DISC_R), (38.0, 27.2, 119.5), 3.0, M_GLOSS, walls=False, seat=(0.0, SEAT))
PADS["T3 Exit"]["links"]["S"] = (3.0, False)
PADS["T0 Landing"]["links"]["N"] = (3.0, False)

soft = ent("Sweeper Softbox", (DISC_C[0], TY1 - 0.05, DISC_C[1]), face_dir(0.0, -1.0, 0.0))
add_area_light(soft, color=(0.85, 0.8, 1.0), intensity=40.0 * K, half_width=1.6, half_height=1.6, draw_range=14.0)

death_zone("Tunnel Pool Death Zone", (TX0, TY0 - 0.1, TZ0), (TX1, TY0 + 0.5, TZ1))
e = ent("Tunnel Probe", ((TX0 + TX1) / 2, (TY0 + TY1) / 2, (TZ0 + TZ1) / 2))
e[wa.TRANSFORM]["scale"] = [(TX1 - TX0) / 2 + T, (TY1 - TY0) / 2 + T, (TZ1 - TZ0) / 2]
add_reflection_probe(e, name_id("prism_probe_tunnel"), capture_offset=(0.0, 3.0, -8.0), resolution=PROBE_RES_128)

# =============================================================================
# IV LANTERN CAVERN   interior x 30..57, z 125.5..152, y 6..36; spiral descends 27 -> 15
# =============================================================================
CUR[0] = folder("IV Lantern Cavern")
CX0, CX1, CZ0, CZ1, CY0, CY1 = 30.0, 57.0, TZ1 + T, 152.0, 6.0, 36.0
slab("Cavern Foundation", (CX0 - T, -1.0, TZ1), (CX1 + T, CY0 - T, CZ1 + T), M_STONE, physics=False)
slab("Cavern Floor", (CX0 - T, CY0 - T, TZ1), (CX1 + T, CY0, CZ1 + T), M_GRAVEL)
slab("Cavern South Wall A", (CX0 - T, CY0, TZ1), (35.5, CY1, CZ0), M_BRICK)
slab("Cavern South Wall B", (40.5, CY0, TZ1), (CX1 + T, CY1, CZ0), M_BRICK)
slab("Cavern South Wall Sill", (35.5, CY0, TZ1), (40.5, 26.4, CZ0), M_BRICK)
slab("Cavern South Wall Lintel", (35.5, 31.0, TZ1), (40.5, CY1, CZ0), M_BRICK)
slab("Cavern North Wall A", (CX0 - T, CY0, CZ1), (35.5, CY1, CZ1 + T), M_BRICK)
slab("Cavern North Wall B", (40.5, CY0, CZ1), (CX1 + T, CY1, CZ1 + T), M_BRICK)
slab("Cavern North Wall Sill", (35.5, CY0, CZ1), (40.5, 13.9, CZ1 + T), M_BRICK)
slab("Cavern North Wall Lintel", (35.5, 19.5, CZ1), (40.5, CY1, CZ1 + T), M_BRICK)
slab("Cavern West Wall", (CX0 - T, CY0, CZ0), (CX0, CY1, CZ1), M_BRICK)
slab("Cavern East Wall", (CX1, CY0, CZ0), (CX1 + T, CY1, CZ1), M_BRICK)
slab("Cavern Roof", (CX0 - T, CY1, TZ1), (CX1 + T, CY1 + T, CZ1 + T), M_BRICK)

SP_OUT, SP_IN, SP_H, SP_SWEEP = 8.0, 3.0, 12.0, 720.0
SP_MID = (SP_OUT + SP_IN) / 2
SP_TOP_Y = 27.0
SP_C = (38.0 + SP_MID, SP_TOP_Y - SP_H, 138.0)   # rotated 180 about Y: entry/exit land at x = 38 heading +Z
SP_STEPS = 64
proc("Spiral Ramp", SP_C, spiral_params(SP_STEPS, SP_H, SP_OUT, SP_IN, SP_H / SP_STEPS, SP_SWEEP, 4, False, True),
     M_SPIRAL, rot=yaw_quat(180.0))
SURFACES.append(("Spiral Ramp", "spiral", None))
proc("Spiral Column", (SP_C[0], (CY0 + CY1) / 2, SP_C[2]), cylinder_params(SP_IN - 0.08, CY1 - CY0, 48), M_COLUMN)
# helical guard rail along the outer edge, one spline per turn (Spline.MaxPoints 64)
RAIL_R, RAIL_LIFT, RAIL_TUBE = SP_OUT - 0.25, 0.45, 0.16
for turn in range(int(SP_SWEEP // 360)):
    pts = []
    for k in range(33):
        a = math.radians(turn * 360.0 + k * 11.25)
        h = SP_H * a / math.radians(SP_SWEEP)
        pts.append((SP_C[0] - RAIL_R * math.cos(a), SP_C[1] + h + RAIL_LIFT, SP_C[2] - RAIL_R * math.sin(a)))
    fields = wa.spline_fields(pts, radius=RAIL_TUBE, sides=10, segments_per_span=6)
    rail = ent(f"Spiral Guard Rail {turn}", (0.0, 0.0, 0.0))
    rail[wa.SPLINE] = {**fields, "material": M_CHROME, "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0]}
    body(rail, [{"type": 3, "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0], "bakedScaleX": 1.0,
                 "bakedScaleY": 1.0, "bakedScaleZ": 1.0, "meshSourceModelId": 0, "proceduralType": 0, "splineParams": fields}], friction=0.02)
    for k in range(0, 32, 4):
        x, y, z = pts[k]
        proc(f"Spiral Rail Post {turn}.{k}", (x, y - RAIL_LIFT / 2, z), cylinder_params(0.05, RAIL_LIFT, 8), M_CHROME, physics=False)

for i in range(11):
    y = CY0 + 2.0 + i * 2.6
    emissive_light(proc(f"Column Band {i}", (SP_C[0], y, SP_C[2]), cylinder_params(SP_IN - 0.04, 0.35, 48), M_BAND, physics=False))

ramp("Spiral Feed", (38.0, 27.2, 124.5), (38.0, SP_TOP_Y, SP_C[2]), 4.0, M_TRACK)
PADS["T3 Exit"]["links"]["N"] = (4.0, True)
checkpoint("T3 Exit", 5, "cavern")

# lanterns: warm sphere lights on chains, scattered outside the spiral
import random
rng = random.Random(7)
lanterns = 0
while lanterns < 22:
    a = rng.uniform(0.0, 2.0 * math.pi)
    r = rng.uniform(SP_OUT + 1.2, 12.5)
    x, z = SP_C[0] + math.cos(a) * r, SP_C[2] + math.sin(a) * r
    if not (CX0 + 1.0 < x < CX1 - 1.0 and CZ0 + 1.0 < z < CZ1 - 1.0):
        continue
    if abs(x - 38.0) < 3.0 and (z < SP_C[2] or z > SP_C[2] + 4.0):
        continue   # keep the feed and exit corridors clear
    y = rng.uniform(12.0, 30.0)
    chain = CY1 - y
    proc(f"Lantern Chain {lanterns}", (x, y + chain / 2, z), cylinder_params(0.025, chain, 6), M_CHAIN, physics=False)
    L = ent(f"Lantern {lanterns}", (x, y, z))
    add_sphere_light(L, color=(1.0, 0.62, 0.3), intensity=2.0 * 65536.0 * 20.0, radius=0.15, draw_range=14.0)
    lanterns += 1

for i, (color, radius, y, speed) in enumerate((((1.0, 0.15, 0.1), 11.0, 24.0, 0.12),
                                                ((0.15, 1.0, 0.3), 10.0, 19.0, 0.10),
                                                ((0.2, 0.35, 1.0), 11.5, 13.5, 0.14))):
    start = (SP_C[0] + radius, y, SP_C[2])
    pts = []
    for j in range(8):
        a = 2.0 * math.pi * j / 8 * (1 if i % 2 == 0 else -1)
        pts.append((math.cos(a) * radius - radius, math.sin(0.5 * j * math.pi) * 1.0, math.sin(a) * radius))
    L = ent(f"Orbit Light {i}", start)
    add_sphere_light(L, color=color, intensity=60.0 * K, radius=0.3, draw_range=22.0)
    add_path_mover(L, pts, speed=speed * 8.0, loop_mode=LOOP_LOOP, mode=1)

exit_glow = ent("Cavern Exit Light", (38.0, 19.3, CZ1 - 0.05), face_dir(0.0, -0.4, -1.0))
add_area_light(exit_glow, color=(0.55, 0.75, 1.0), intensity=30.0 * K, half_width=2.2, half_height=0.3, draw_range=16.0)

death_zone("Cavern Floor Death Zone", (CX0, CY0 - 0.1, CZ0), (CX1, CY0 + 0.5, CZ1))
e = ent("Cavern Probe", ((CX0 + CX1) / 2, (CY0 + CY1) / 2, (CZ0 + CZ1) / 2))
e[wa.TRANSFORM]["scale"] = [(CX1 - CX0) / 2 + T, (CY1 - CY0) / 2 + T, (CZ1 - CZ0) / 2 + T]
add_reflection_probe(e, name_id("prism_probe_cavern"), capture_offset=(-9.5, 0.0, -9.0), resolution=PROBE_RES_128)

# =============================================================================
# V  GOLDEN FINISH
# =============================================================================
CUR[0] = folder("V Golden Finish")
pad("F0 Outlet", (38.0, 14.6, 160.0), (6.0, 6.0), pillar=True)
ramp("Spiral Outlet", (38.0, SP_TOP_Y - SP_H, SP_C[2]), edge_point(PADS["F0 Outlet"], (0.0, -1.0)), 4.0, M_TRACK,
     seat=(SEAT, SEAT))   # start reaches back under the bottom tread, which rises away from it
PADS["F0 Outlet"]["links"]["S"] = (4.0, True)
pad("G0 Dock", (50.0, 14.6, 160.0), (4.0, 4.0), walls="NS", pillar=True, depth=4.0)
pad("G1 Dock", (66.0, 14.6, 160.0), (4.0, 4.0), walls="NS", pillar=True, depth=4.0)
pad("FIN", (80.0, 14.0, 160.0), (14.0, 14.0), M_STONE, pillar=True, depth=4.2)
link("F0 Outlet", "G0 Dock", 3.0)
link("G0 Dock", "G1 Dock", 4.0, walls=False, gap=True)
link("G1 Dock", "FIN", 4.0)
checkpoint("F0 Outlet", 6, "outlet")
checkpoint("FIN", 100, "finish")
sign("Sign V", (38.0, 17.6, 163.4), "V  GOLDEN FINISH", FACE_NEG_Z, scale=0.6)

# stepping stones across the gap, alternately risen flush and sunk 2.5 m, tall enough (and the docks deep enough)
# that every sunk slot is walled in; they only move vertically,
# because a ball on a platform sliding sideways just rolls in place and drops off the trailing edge
STONE_X0, STONE_X1, STONE_GAP, STONE_DROP = 52.05, 63.95, 0.05, 2.5
stone_w = (STONE_X1 - STONE_X0 - 3 * STONE_GAP) / 4
for i in range(4):
    x0 = STONE_X0 + i * (stone_w + STONE_GAP)
    sunk = i % 2 == 1
    top = 14.6 - (STONE_DROP if sunk else 0.0)
    e = slab(f"Stepping Stone {i}", (x0, top - 1.2 - STONE_DROP, 158.0), (x0 + stone_w, top, 162.0), M_PUSHER, motion=1)
    add_path_mover(e, [(0.0, 0.0, 0.0), (0.0, STONE_DROP if sunk else -STONE_DROP, 0.0)], speed=0.45, wait_time=1.6,
                   easing=EASE_IN_OUT_SINE, loop_mode=LOOP_PINGPONG, mode=0)

proc("Finish Arch", (76.0, 14.0, 160.0), torus_params(3.6, 0.3, 64, 24), M_GOLD, rot=yaw_quat(90.0))
sign("Finish Text", (76.0, 18.6, 160.0), "FINISH", FACE_NEG_X, scale=1.2, material=M_FINISH, depth=0.25)
for i, (x, z, r, m) in enumerate(((75.0, 155.0, 0.6, M_CHROME), (75.0, 165.0, 0.6, M_CHROME),
                                   (84.5, 155.2, 0.5, M_GOLD), (84.5, 164.8, 0.5, M_CRATES[1]))):
    e = ent(f"Finish Ball {i}", (x, 14.0 + r, z))
    mesh(e, sphere_params(r, 48, 32), m)
    body(e, [sphere_shape(r)], motion=2, mass=4.0, friction=0.6, restitution=0.3)
# crate pyramid at the end of the finish pad: 4-3-2-1, ball smashes through it
S = 0.7
n = 0
for row in range(4):
    for k in range(4 - row):
        z = 160.0 - (4 - row - 1) * (S + 0.02) / 2 + k * (S + 0.02) - S / 2
        e = ent(f"Crate {n}", (84.0, 14.0 + row * (S + 0.005), z))
        mesh(e, box_params(S, S, S), M_CRATES[n % len(M_CRATES)])
        body(e, [box_shape((S, S, S))], motion=2, mass=2.0, friction=0.6)
        n += 1

pad_walls()

# =============================================================================
# verification: every segment end must sit exactly on a pad edge (or a known special surface)
# =============================================================================
def on_pad_edge(p, tol=1e-4):
    for name, kind, d in SURFACES:
        if kind != "pad":
            continue
        x0, x1, z0, z1, y = d
        if abs(p[1] - y) > tol:
            continue
        on_x_edge = (abs(p[0] - x0) < tol or abs(p[0] - x1) < tol) and z0 - tol <= p[2] <= z1 + tol
        on_z_edge = (abs(p[2] - z0) < tol or abs(p[2] - z1) < tol) and x0 - tol <= p[0] <= x1 + tol
        if on_x_edge or on_z_edge:
            return name
    return None

def spiral_top_bottom():
    local_at, entry, exit_ = wa.spiral_math(SP_OUT, SP_H, SP_SWEEP, sample_radius=SP_MID)
    def world(lp):   # 180 deg yaw: (x, z) -> (-x, -z)
        return (SP_C[0] - lp[0], SP_C[1] + lp[1], SP_C[2] - lp[2])
    return world(entry), world(exit_)

def verify():
    top, bottom = spiral_top_bottom()
    assert all(abs(a - b) < 1e-4 for a, b in zip(top, (38.0, SP_TOP_Y, SP_C[2]))), top
    assert all(abs(a - b) < 1e-4 for a, b in zip(bottom, (38.0, SP_TOP_Y - SP_H, SP_C[2]))), bottom
    specials = {"spiral": [top, bottom], "disc": [(DISC_C[0], DISC_Y, DISC_C[1] - DISC_R), (DISC_C[0], DISC_Y, DISC_C[1] + DISC_R)]}
    problems = []
    for name, kind, d in SURFACES:
        if kind != "ramp":
            continue
        start, end, width = d
        run = math.hypot(end[0] - start[0], end[2] - start[2])
        slope = math.degrees(math.atan2(start[1] - end[1], run))
        for label, p in (("start", start), ("end", end)):
            hit = on_pad_edge(p) or next((k for k, pts in specials.items()
                                          if any(all(abs(a - b) < 1e-4 for a, b in zip(p, q)) for q in pts)), None)
            if hit is None and not name.startswith("Catwalk"):
                problems.append(f"{name} {label} {p} touches nothing")
        if slope > 13.0 or slope < -0.5:
            problems.append(f"{name} slope {slope:.1f} deg")
        print(f"  {name:28s} len {run:5.1f}  drop {start[1] - end[1]:4.1f}  slope {slope:4.1f}  width {width}")
    # spiral clearance: consecutive turns are SP_H/2 apart, feed and outlet pass over/under a turn
    assert SP_H / 2 > 3.0
    for p in problems:
        print("  PROBLEM:", p)
    assert not problems

def zfight_check(tol=1e-4):
    """Two render boxes whose faces lie in the same plane, face the same way and overlap in area z-fight."""
    hits = []
    for i in range(len(AABBS)):
        na, la, ha = AABBS[i]
        for j in range(i + 1, len(AABBS)):
            nb, lb, hb = AABBS[j]
            for ax in range(3):
                o = [k for k in range(3) if k != ax]
                if any(min(ha[k], hb[k]) - max(la[k], lb[k]) <= tol for k in o):
                    continue
                for tag, fa, fb in (("min", la[ax], lb[ax]), ("max", ha[ax], hb[ax])):
                    if abs(fa - fb) < tol:
                        hits.append(f"{na} / {nb}: {'xyz'[ax]}{tag} = {fa:.3f}")
    for h in hits:
        print("  Z-FIGHT:", h)
    return hits

print("segments:")
verify()
print(f"z-fight check over {len(AABBS)} boxes:")
zfight_check()

if len(sys.argv) > 2 and sys.argv[1] == "--test-spawn":
    label = sys.argv[2]
    pos = next((p for pr, l, p in CHECKPOINTS if l == label), None) or tuple(float(v) for v in label.split(","))
    t = base_entity("Test Spawn", pos)
    t[SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 50}
    entities.append(t)
    SCENE_PATH = os.path.join(REPO, "scenes", "_prism_test.wscene")
    SCENE_NAME, SCENE_ID = "Prism Run Test", name_id("prism_run_test")

camera = {"rotation": list(wa.camera_look_quat(-40.0, -38.0, 95.0)), "translation": [75.0, 70.0, -10.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, SCENE_NAME, editor_camera=camera)
print(f"wrote {SCENE_PATH}: {len(entities)} entities, {len(CHECKPOINTS)} checkpoints")
