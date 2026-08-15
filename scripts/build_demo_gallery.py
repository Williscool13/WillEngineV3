#!/usr/bin/env python3
"""
Builds the demo gallery showcase scene: seven halls on one promenade (x runs
west -> east, corridors at z 6..10), separated by 4m-deep solid vestibules
(light locks). Camera enters at x<0 and exits east onto the open terrace.

  PRISM    x   0..20    diffuse GI: sun windows + louver crossfade red/cobalt
  LUMEN    x  24..46    ReSTIR DI on WHITE analytic lights: area panels + sphere carousels
    ANNEXES (detached islands z 30..39, never framed): A = emissive tri-light
    reference, B = colored analytic lights (chroma variance reference)
  MIRROR   x  50..70    traced reflections only: roughness ramp capped at 0.4
  VAULT    x  74..90    reflection probes: rough-metal ramp + nested colored alcoves
  FOUNDRY  x  94..112   procedural mesh zoo on labeled plinths
  COURT    x 116..138   open-roof: sun + colonnade + relief + spline ribbon
  TERRACE  x 142..168   open sky, NO walls: 5x5 PBR sphere matrix + end card

Brightness rule (GI is weak in low light): every hall has a generously lit
floor; direct light does the heavy lifting, GI only carries bounce.
Emitter rule: demo emitters are large-area (blades/panels); tiny scattered
emissives are ReSTIR worst case and live only in the ANNEX as a reference.
Run from repo root:  python scripts/build_demo_gallery.py
Design: .claude/plans/demo_gallery_scene_design_2026-08-08.md (v4 layout appended)
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("demo_gallery")   # must precede every next_id() call; see seed_ids()

from wscene_authoring import (
    base_entity, box_params, cylinder_params, sphere_params, torus_params, next_id, name_id,
    PROCEDURAL, PHYSICS, FOLDER, SCENE_FOLDER, TEXT3D, SPLINE,
    add_directional_light, add_sphere_light, add_area_light, add_path_mover,
    add_reflection_probe, add_local_ddgi_volume, add_world_text, spline_fields,
    PROBE_RES_128, PROBE_RES_256, EASE_IN_OUT_SINE, EASE_LINEAR, LOOP_PINGPONG, LOOP_LOOP,
    ALIGN_CENTER, ANCHOR_BOTTOM, ANCHOR_CENTER,
)

ROBOTO_FONT = 11302268835193496650   # assets/fonts/Roboto/Roboto.wsfont (baked FontID)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "demo_gallery.wscene")
SCENE_ID = name_id("demo_gallery")

H = 6.0      # interior height everywhere (court walls same, no roof)
T = 0.5      # wall thickness (GI corner-leak guard)
ZW = 16.0    # interior width, all halls
VD = 4.0     # vestibule depth (solid separator block with 4x4 corridor)
SUN_EL = math.radians(35.0)

# ---- materials (name_id-keyed, safe to regenerate) ----
# Floors + ceilings carry the sandbox concrete set for breakup (parallel camera
# motion over featureless surfaces has nothing to anchor reprojection); walls stay
# flat. Reflection exhibit floors (mirror strips, vault slab) get ALBEDO ONLY:
# their roughness ramps and flat normals ARE the exhibit, so no arm/normal maps.
def _pbr_set(set_name):
    diff = wa.write_texture_stub(f"{set_name}_diff", f"src/{set_name}/{set_name}_diff_1k.png", wa.DXGI_BC7_UNORM_SRGB)
    nor = wa.write_texture_stub(f"{set_name}_nor", f"src/{set_name}/{set_name}_nor_1k.png", wa.DXGI_BC5_UNORM)
    arm = wa.write_texture_stub(f"{set_name}_arm", f"src/{set_name}/{set_name}_arm_1k.png", wa.DXGI_BC7_UNORM)
    return diff, nor, arm

T_CONCRETE = _pbr_set("concrete")

MAT_WHITE = wa.write_material("gallery_white", base_color=(0.72, 0.70, 0.66, 1.0), roughness=0.9)
# tinted so map albedo lands at the same 0.72 white as the walls
MAT_CEILING = wa.write_material("gallery_ceiling", base_color=(1.3, 1.27, 1.2, 1.0), roughness=0.9,
                                albedo_tex=T_CONCRETE[0], metal_rough_tex=T_CONCRETE[2], normal_tex=T_CONCRETE[1],
                                uv_scale=(0.5, 0.5))
# tint lifts the map back to the old 0.62 flat albedo so PRISM's bounce budget survives
MAT_FLOOR = wa.write_material("gallery_floor", base_color=(1.12, 1.10, 1.06, 1.0), roughness=1.0,
                              albedo_tex=T_CONCRETE[0], metal_rough_tex=T_CONCRETE[2], normal_tex=T_CONCRETE[1],
                              uv_scale=(0.5, 0.5))
MAT_RED = wa.write_material("gallery_red", base_color=(0.78, 0.05, 0.04, 1.0), roughness=0.85)
MAT_COBALT = wa.write_material("gallery_cobalt", base_color=(0.06, 0.14, 0.72, 1.0), roughness=0.85)
MAT_CHARCOAL = wa.write_material("gallery_charcoal", base_color=(0.10, 0.10, 0.11, 1.0), roughness=0.6)
MAT_SLATE = wa.write_material("gallery_slate", base_color=(0.30, 0.30, 0.33, 1.0), roughness=0.8)
MAT_SLATE_FLOOR = wa.write_material("gallery_slate_floor", base_color=(0.62, 0.60, 0.58, 1.0), roughness=0.7,
                                    albedo_tex=T_CONCRETE[0], metal_rough_tex=T_CONCRETE[2], normal_tex=T_CONCRETE[1],
                                    uv_scale=(0.5, 0.5))
MAT_MIRROR = wa.write_material("gallery_mirror", base_color=(0.95, 0.95, 0.95, 1.0), metallic=1.0, roughness=0.02)
MAT_BRUSHED = wa.write_material("gallery_brushed", base_color=(0.60, 0.60, 0.62, 1.0), metallic=1.0, roughness=0.3)
MAT_GOLD = wa.write_material("gallery_gold", base_color=(1.0, 0.78, 0.35, 1.0), metallic=1.0, roughness=0.15)
MAT_RIBBON = wa.write_material("gallery_ribbon", base_color=(0.55, 0.06, 0.05, 1.0), roughness=0.25)
MAT_BEAM_WARM = wa.write_material("gallery_beam_warm", base_color=(0.0, 0.0, 0.0, 1.0), emissive=(1.0, 0.62, 0.28, 8.0))
MAT_BEAM_TEAL = wa.write_material("gallery_beam_teal", base_color=(0.0, 0.0, 0.0, 1.0), emissive=(0.25, 0.85, 1.0, 8.0))
MAT_HALO = wa.write_material("gallery_halo", base_color=(0.0, 0.0, 0.0, 1.0), emissive=(1.0, 0.88, 0.72, 10.0))
MAT_VAULT_FLOOR = wa.write_material("gallery_vault_floor", base_color=(1.0, 1.0, 1.04, 1.0), roughness=0.45,
                                    albedo_tex=T_CONCRETE[0], uv_scale=(0.5, 0.5))
# mirror ramp stays at/below the traced-reflection threshold (0.4); probe-fed roughness lives in VAULT
GLOSS_ROUGH = [0.4, 0.34, 0.28, 0.22, 0.17, 0.12, 0.07, 0.02]
MAT_GLOSS = [wa.write_material(f"gallery_gloss_{i}", base_color=(0.06, 0.06, 0.07, 1.0), roughness=r) for i, r in enumerate(GLOSS_ROUGH)]
MAT_PROBE_R = [wa.write_material(f"gallery_probe_r{i}", base_color=(0.90, 0.90, 0.92, 1.0), metallic=1.0, roughness=0.45 + 0.1 * i)
               for i in range(5)]
MAT_PBR = {(i, j): wa.write_material(f"gallery_pbr_m{i}r{j}", base_color=(0.85, 0.85, 0.88, 1.0),
                                     metallic=i / 4.0, roughness=max(j / 4.0, 0.05))
           for i in range(5) for j in range(5)}

# =============================================================================
# helpers (corner-pivot; analytic physics idioms proven in build_lighting_lab.py)
# =============================================================================
entities = []

def _mesh(entity, ptype_idx, fields, material):
    entity[PROCEDURAL] = {**fields, "material": material,
                          "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0], "type": ptype_idx}
    return entity

MIN_HALF_EXTENT = 0.06   # Jolt default convex radius floor; thinner = render-only

def _box_physics(entity, size, motion=0):
    hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    if min(hx, hy, hz) < MIN_HALF_EXTENT:
        return entity
    shape = {"type": 0, "halfExtents": [hx, hy, hz], "offset": [hx, hy, hz], "rotation": [1.0, 0.0, 0.0, 0.0],
             "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}
    entity[PHYSICS] = {"motionType": motion, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                       "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape]}
    return entity

def box(name, corner, size, material, physics=True, rot=(1.0, 0.0, 0.0, 0.0)):
    """_box_physics assumes identity rotation + corner pivot, so only pass a non-identity rot with physics=False."""
    e = base_entity(name, corner, rot)
    fields, idx = box_params(size[0], size[1], size[2])
    _mesh(e, idx, fields, material)
    if physics:
        _box_physics(e, size)
    entities.append(e)
    return e

def _sphere_physics(entity, radius, motion=0):
    shape = {"type": 1, "radius": radius, "offset": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0],
             "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}
    entity[PHYSICS] = {"motionType": motion, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                       "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape]}
    return entity

def sphere(name, center, radius, material, physics=True):
    e = base_entity(name, center)
    fields, idx = sphere_params(radius, 64, 64)
    _mesh(e, idx, fields, material)
    if physics:
        _sphere_physics(e, radius)
    entities.append(e)
    return e

def face_dir(dx, dy, dz):
    """[w,x,y,z] mapping local +Z to a world dir (sun direction / area-light normal)."""
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / n, dy / n, dz / n
    if dz < -0.999999:
        return [0.0, 0.0, 1.0, 0.0]
    cx, cy = -dy, dx
    w = 1.0 + dz
    n = math.sqrt(w * w + cx * cx + cy * cy)
    return [w / n, cx / n, cy / n, 0.0]

FACE_WEST = (0.70710678, 0.0, -0.70710678, 0.0)   # glyph face (+Z local) -> -X, reading dir -> +Z
FACE_SOUTH = (0.0, 0.0, 1.0, 0.0)                 # glyph face -> -Z (read from inside a hall looking north)

def sign3d(name, pos, text, scale=0.8, material=MAT_WHITE, rot=FACE_WEST, anchor=ANCHOR_CENTER):
    e = base_entity(name, pos, rot)
    e[TEXT3D] = {"depth": 0.12, "flatness": 0.0005, "fontId": ROBOTO_FONT, "material": material,
                 "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0],
                 "scale": scale, "smoothNormals": True, "text": text, "tracking": 0.08,
                 "align": ALIGN_CENTER, "anchor": anchor, "wrapWidth": 0.0, "bendRadius": 0.0}
    entities.append(e)
    return e

def light_entity(name, pos, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot)
    entities.append(e)
    return e

def separator(name, x0, opening_h=4.0):
    """4m-deep solid light-lock between halls, 4x4 corridor tunneled through at z 6..10."""
    box(f"{name} S", (x0, 0.0, -T), (VD, H, 6.0 + T), MAT_WHITE)
    box(f"{name} N", (x0, 0.0, 10.0), (VD, H, 6.0 + T), MAT_WHITE)
    box(f"{name} Lintel", (x0, opening_h, 6.0), (VD, H - opening_h, 4.0), MAT_WHITE)

def blade(name, cx, cz, yaw_deg, w, h, d, material):
    """Emissive slab, center-bottom at (cx, 0.3, cz) on a charcoal plinth, yawed about Y. Render-only."""
    box(f"{name} Plinth", (cx - 0.55, 0.05, cz - 0.4), (1.1, 0.25, 0.8), MAT_CHARCOAL)
    th = math.radians(yaw_deg)
    hx, hz = w * 0.5, d * 0.5
    rx = hx * math.cos(th) + hz * math.sin(th)
    rz = -hx * math.sin(th) + hz * math.cos(th)
    rot = (math.cos(th * 0.5), 0.0, math.sin(th * 0.5), 0.0)
    e = base_entity(name, (cx - rx, 0.3, cz - rz), rot)
    f, i = box_params(w, h, d)
    _mesh(e, i, f, material)
    entities.append(e)
    return e

def stage_disc(name, cx, cz, r=3.0):
    e = base_entity(name, (cx, 0.095, cz))
    f, i = cylinder_params(r, 0.08, slices=48)
    _mesh(e, i, f, MAT_CHARCOAL)
    entities.append(e)
    return e

def circle_points(cx, y, cz, r, n=8, phase=0.0):
    return [(cx + r * math.cos(phase + 2.0 * math.pi * i / n), y, cz + r * math.sin(phase + 2.0 * math.pi * i / n))
            for i in range(n)]

def carousels(tag, cx, cz, rings, colors):
    """rings = [(radius, height, speed; negative = opposite direction)]; 4 sphere lights each."""
    for ci, (r, y, speed) in enumerate(rings):
        for li in range(4):
            phase = 2.0 * math.pi * li / 4.0
            pts = circle_points(cx, y, cz, r, n=8, phase=phase)
            if speed < 0.0:
                pts = list(reversed(pts))
            e = light_entity(f"[{tag}] Orbit {ci}-{li}", pts[0])
            add_sphere_light(e, color=colors[li % len(colors)], intensity=25.0, radius=0.3, draw_range=14.0)
            add_path_mover(e, pts, speed=abs(speed), wait_time=0.0, easing=EASE_LINEAR, loop_mode=LOOP_LOOP)

def gi_volumes(tag, region_min, region_max, spacing=2.0):
    tiles = wa.gi_volume_grid(region_min, region_max, spacing)
    for i, (lo, hi) in enumerate(wa.gi_volume_tiles(region_min, region_max, tiles)):
        corner = wa.gi_volume_window(lo, hi, spacing)
        e = base_entity(f"[{tag}] GI Volume {i}", corner)
        add_local_ddgi_volume(e, name_id(f"gallery_gi_{tag}_{i}"), probe_spacing=spacing)
        entities.append(e)

PROBE_EMBED = 0.15   # proxy faces sit slightly INSIDE the walls; a face coincident with the interior surface flips under jitter

def hall_probe(tag, x0, x1, resolution, capture=(0.0, 0.0, 0.0)):
    e = base_entity(f"[{tag}] Probe", ((x0 + x1) * 0.5, H * 0.5, ZW * 0.5),
                    scale=((x1 - x0) * 0.5 + PROBE_EMBED, H * 0.5 + PROBE_EMBED, ZW * 0.5 + PROBE_EMBED))
    add_reflection_probe(e, name_id(f"gallery_probe_{tag}"), capture_offset=capture, resolution=resolution)
    entities.append(e)

def room_probe(tag, center, half, capture):
    e = base_entity(f"[{tag}] Probe", center, scale=(half[0] + PROBE_EMBED, half[1] + PROBE_EMBED, half[2] + PROBE_EMBED))
    add_reflection_probe(e, name_id(f"gallery_probe_{tag}"), capture_offset=capture, resolution=PROBE_RES_128)
    entities.append(e)

def hall_ceiling(tag, x0, x1):
    box(f"[{tag}] Ceiling", (x0, H, -T), (x1 - x0, T, ZW + 2 * T), MAT_CEILING)

FOLDER_TAGS = {}
def tag_folder(tag, start_index):
    FOLDER_TAGS.setdefault(tag, []).append((start_index, len(entities)))

# =============================================================================
# shared shell: floor + entry + sun + vestibule separators
# =============================================================================
_start = len(entities)
box("[Shared] Floor", (-T, -T, -T), (142.0 + T, T, ZW + 2 * T), MAT_FLOOR)   # ends flush at 142 where the terrace slab starts (coplanar overlap = z-fight)
box("[Shared] Entry Apron", (-8.0, -T, 3.0), (8.0 - T, T, 10.0), MAT_FLOOR)

# west wall: 4x4m entry threshold centered on z=8
box("[Shared] West Wall S", (-T, 0.0, -T), (T, H, 6.0 + T), MAT_WHITE)
box("[Shared] West Wall N", (-T, 0.0, 10.0), (T, H, 6.0 + T), MAT_WHITE)
box("[Shared] West Lintel", (-T, 4.0, 6.0), (T, H - 4.0, 4.0), MAT_WHITE)

separator("[Shared] Vestibule PL", 20.0)    # PRISM -> LUMEN
separator("[Shared] Vestibule LM", 46.0)    # LUMEN -> MIRROR
separator("[Shared] Vestibule MV", 70.0)    # MIRROR -> VAULT
separator("[Shared] Vestibule VF", 90.0)    # VAULT -> FOUNDRY
separator("[Shared] Vestibule FC", 112.0)   # FOUNDRY -> COURT
separator("[Shared] Gate", 138.0, opening_h=5.0)   # COURT -> TERRACE, frames sky

# sun: from the south at 35 deg, slight east drift so shaft edges aren't axis-locked
sun = light_entity("[Shared] Sun", (45.0, 30.0, -20.0))
sun[wa.TRANSFORM]["rotation"] = list(face_dir(0.12, -math.sin(SUN_EL), math.cos(SUN_EL)))
add_directional_light(sun, color=(1.0, 0.96, 0.88), intensity=10.0, priority=0, angular_radius_deg=0.75)
tag_folder("Shared", _start)

# =============================================================================
# hall 1: PRISM (x 0..20). Two 5x3m windows LOW in the south wall (y 2.5..5.5):
# most of each shaft lands on the white floor (that patch is the bounce engine,
# ~2/3 of the flux); the upper band paints the 3x3.5m panel at z=4.5. The louver
# outside PingPongs to cover one window at a time -> room crossfades red/cobalt.
# =============================================================================
_start = len(entities)
PX0, PX1 = 0.0, 20.0
WIN_A, WIN_B = 4.0, 11.0    # window x starts, both 5 wide
sw = base_entity("[Prism] South Wall", (PX0, 0.0, -T))
fields, idx = wa.wall_params(PX1 - PX0, H, T, openings=[(WIN_A, 2.5, 5.0, 3.0), (WIN_B, 2.5, 5.0, 3.0)])
wa.add_procedural(sw, idx, fields)
sw[PROCEDURAL]["material"] = MAT_WHITE
entities.append(sw)
box("[Prism] North Wall", (PX0, 0.0, ZW), (PX1 - PX0, H, T), MAT_WHITE)
hall_ceiling("Prism", PX0, PX1)

def angled_panel(name, cx, cz, yaw_deg, material):
    """3x3.5m panel, center-bottom at (cx, 0, cz), yawed about Y. Inner ends pushed
    north so the pair opens toward the sun wall like a book."""
    sx, sy, sz = 3.0, 3.5, 0.15
    th = math.radians(yaw_deg)
    hx, hz = sx * 0.5, sz * 0.5
    rx = hx * math.cos(th) + hz * math.sin(th)
    rz = -hx * math.sin(th) + hz * math.cos(th)
    rot = (math.cos(th * 0.5), 0.0, math.sin(th * 0.5), 0.0)
    e = base_entity(name, (cx - rx, 0.0, cz - rz), rot)
    f, i = box_params(sx, sy, sz)
    _mesh(e, i, f, material)
    entities.append(e)
    return e

angled_panel("[Prism] Panel Red", WIN_A + 2.5, 4.5, -12.0, MAT_RED)
angled_panel("[Prism] Panel Cobalt", WIN_B + 2.5, 4.5, 12.0, MAT_COBALT)

# louver blade outside the south wall (0.02m gap), covers one window per end of travel
louver = base_entity("[Prism] Louver Blade", (3.5, 2.0, -0.67))
fields, idx = box_params(6.0, 4.0, 0.15)
_mesh(louver, idx, fields, MAT_CHARCOAL)
add_path_mover(louver, [(3.5, 2.0, -0.67), (10.5, 2.0, -0.67)], speed=0.8, wait_time=1.5,
               easing=EASE_IN_OUT_SINE, loop_mode=LOOP_PINGPONG, mode=0)
entities.append(louver)

def plinth_sphere(tag, px, pz, plinth_side, plinth_h, r):
    box(f"[Prism] Plinth {tag}", (px - plinth_side * 0.5, 0.0, pz - plinth_side * 0.5),
        (plinth_side, plinth_h, plinth_side), MAT_WHITE)
    sphere(f"[Prism] Sphere {tag}", (px, plinth_h + r, pz), r, MAT_WHITE)

plinth_sphere("S", 15.3, 13.5, 0.9, 1.2, 0.5)
plinth_sphere("M", 18.0, 11.5, 1.6, 0.6, 1.0)
plinth_sphere("L", 16.2, 8.5, 2.4, 0.3, 1.5)

sign3d("[Prism] Sign", (-0.62, 5.0, 8.0), "PRISM")
gi_volumes("Prism", (PX0 - T, -T, -T), (PX1 + T, H + T, ZW + T))
hall_probe("Prism", PX0, PX1, PROBE_RES_256)
tag_folder("Prism", _start)

# =============================================================================
# hall 2: LUMEN (x 24..46). ReSTIR DI on ANALYTIC lights only: two tilted
# skylight area panels, a gallery of three colored wall panels each washing a
# matte sculpture (soft-shadow showcase), one traveling overhead panel (moving
# soft shadows = antilag), and two counter-rotating sphere-light carousels
# around a trefoil centerpiece. All light intensities are exposure guesses.
# =============================================================================
_start = len(entities)
LX0, LX1 = 24.0, 46.0
box("[Lumen] South Wall", (LX0, 0.0, -T), (LX1 - LX0, H, T), MAT_SLATE)
box("[Lumen] North Wall", (LX0, 0.0, ZW), (LX1 - LX0, H, T), MAT_SLATE)
box("[Lumen] Floor Slab", (LX0, 0.0, 0.0), (LX1 - LX0, 0.05, ZW), MAT_SLATE_FLOOR)
hall_ceiling("Lumen", LX0, LX1)

# LUMEN is white-light ONLY (user ruling: chroma variance lives in the chroma annex)
sky_a = light_entity("[Lumen] Skylight A", (29.0, 5.2, 3.5), tuple(face_dir(2.5, -5.2, 5.0)))
add_area_light(sky_a, color=(1.0, 1.0, 1.0), intensity=30.0, half_width=2.0, half_height=1.0, draw_range=20.0)
sky_b = light_entity("[Lumen] Skylight B", (41.0, 5.2, 12.5), tuple(face_dir(-2.5, -5.2, -5.0)))
add_area_light(sky_b, color=(1.0, 1.0, 1.0), intensity=30.0, half_width=2.0, half_height=1.0, draw_range=20.0)

GALLERY = [(27.5, (1.0, 1.0, 1.0), "sphere"), (33.0, (1.0, 1.0, 1.0), "torus"), (41.0, (1.0, 1.0, 1.0), "dodeca")]
for gx, color, shape_name in GALLERY:
    p = light_entity(f"[Lumen] Panel {shape_name}", (gx, 3.0, 0.55))
    add_area_light(p, color=color, intensity=25.0, half_width=1.5, half_height=1.0, draw_range=18.0)
    box(f"[Lumen] Pedestal {shape_name}", (gx - 0.5, 0.05, 3.1), (1.0, 0.9, 1.0), MAT_CHARCOAL)
    if shape_name == "sphere":
        sphere(f"[Lumen] Sculpt {shape_name}", (gx, 1.45, 3.6), 0.5, MAT_WHITE, physics=False)
    elif shape_name == "torus":
        e = base_entity(f"[Lumen] Sculpt {shape_name}", (gx, 1.6, 3.6))
        f, i = torus_params(0.45, 0.16)
        _mesh(e, i, f, MAT_WHITE)
        entities.append(e)
    else:
        e = base_entity(f"[Lumen] Sculpt {shape_name}", (gx, 1.5, 3.6))
        f, i = wa.dodeca_params(0.5)
        _mesh(e, i, f, MAT_WHITE)
        entities.append(e)

traveler = light_entity("[Lumen] Traveler", (26.0, 5.4, 8.0), tuple(face_dir(0.0, -1.0, 0.0)))
add_area_light(traveler, color=(1.0, 1.0, 1.0), intensity=30.0, half_width=1.2, half_height=1.2, draw_range=20.0)
add_path_mover(traveler, [(26.0, 5.4, 8.0), (44.0, 5.4, 8.0)], speed=1.0, wait_time=1.0,
               easing=EASE_IN_OUT_SINE, loop_mode=LOOP_PINGPONG)

stage_disc("[Lumen] Stage", 35.0, 8.0)
trefoil = base_entity("[Lumen] Trefoil", (35.0, 1.6, 8.0))   # center pivot, eyeball height in editor
fields, idx = wa.trefoil_params(0.45, 0.14)
_mesh(trefoil, idx, fields, MAT_WHITE)
entities.append(trefoil)
carousels("Lumen", 35.0, 8.0, [(2.4, 2.3, 2.2), (1.6, 3.4, -2.8)], [(1.0, 1.0, 1.0)])

sign3d("[Lumen] Sign", (19.88, 5.0, 8.0), "LUMEN")
gi_volumes("Lumen", (LX0 - T, -T, -T), (LX1 + T, H + T, ZW + T))
hall_probe("Lumen", LX0, LX1, PROBE_RES_128, capture=(-4.0, 0.0, 0.0))   # off the trefoil/stage axis
tag_folder("Lumen", _start)

# =============================================================================
# annexes (detached reference islands north of the shell at z 30..39, NOT on
# the dolly path, never framed). Annex A = emissive tri-lights in isolation.
# Annex B = the colored analytic lights evicted from LUMEN (chroma variance
# reference). Own doors face the promenade; no connection to the shell.
# =============================================================================
def annex_shell(tag, x0, z0, w, d, label):
    box(f"[{tag}] Floor", (x0 - T, -T, z0 - T - 2.0), (w + 2 * T, T, d + 2 * T + 2.0), MAT_FLOOR)
    sw = base_entity(f"[{tag}] South Wall", (x0 - T, 0.0, z0 - T))
    fields, idx = wa.wall_params(w + 2 * T, H, T, openings=[((w + 2 * T) * 0.5 - 1.25, 0.0, 2.5, 3.0)])
    wa.add_procedural(sw, idx, fields)
    sw[PROCEDURAL]["material"] = MAT_SLATE
    entities.append(sw)
    box(f"[{tag}] West Wall", (x0 - T, 0.0, z0), (T, H, d), MAT_SLATE)
    box(f"[{tag}] East Wall", (x0 + w, 0.0, z0), (T, H, d), MAT_SLATE)
    box(f"[{tag}] North Wall", (x0 - T, 0.0, z0 + d), (w + 2 * T, H, T), MAT_SLATE)
    box(f"[{tag}] Ceiling", (x0 - T, H, z0 - T), (w + 2 * T, T, d + 2 * T), MAT_CEILING)
    box(f"[{tag}] Floor Slab", (x0, 0.0, z0), (w, 0.05, d), MAT_SLATE_FLOOR)
    sign3d(f"[{tag}] Sign", (x0 + w * 0.5, 3.6, z0 - T - 0.12), label, scale=0.35, rot=FACE_SOUTH)

AZ0, AD = 30.0, 9.0

_start = len(entities)
annex_shell("Annex", 35.0, AZ0, 10.0, AD, "EMISSIVE")
rng = random.Random(1337)
BLADE_SPOTS = [(36.5, 37.8), (39.5, 38.1), (42.5, 37.7), (36.3, 31.5), (44.0, 32.0), (44.2, 36.5)]
for bi, (bx, bz) in enumerate(BLADE_SPOTS):
    mat = MAT_BEAM_TEAL if rng.random() < 0.25 else MAT_BEAM_WARM
    blade(f"[Annex] Blade {bi}", bx + rng.uniform(-0.2, 0.2), bz + rng.uniform(-0.2, 0.2),
          rng.uniform(-14.0, 14.0), 0.7, rng.uniform(2.8, 4.6), 0.14, mat)

stage_disc("[Annex] Stage", 40.0, 34.5)
halo = base_entity("[Annex] Halo", (40.0, 4.3, 34.5), (0.70710678, 0.70710678, 0.0, 0.0))
fields, idx = torus_params(2.4, 0.18, slices=48, stacks=24)
_mesh(halo, idx, fields, MAT_HALO)
entities.append(halo)

gi_volumes("Annex", (34.5, -T, AZ0 - T), (45.5, H + T, AZ0 + AD + T))
room_probe("Annex", (40.0, H * 0.5, AZ0 + AD * 0.5), (5.0, H * 0.5, AD * 0.5), (0.0, 0.0, -3.2))
tag_folder("Annex", _start)

_start = len(entities)
annex_shell("AnnexChroma", 48.0, AZ0, 10.0, AD, "CHROMA")
for gx, color, cname in ((49.5, (1.0, 0.75, 0.40), "amber"), (53.0, (0.40, 0.70, 1.00), "cyan"), (56.5, (1.0, 0.35, 0.60), "magenta")):
    p = light_entity(f"[AnnexChroma] Panel {cname}", (gx, 3.0, AZ0 + 0.55))
    add_area_light(p, color=color, intensity=25.0, half_width=1.5, half_height=1.0, draw_range=18.0)

stage_disc("[AnnexChroma] Stage", 53.0, 34.5)
box("[AnnexChroma] Plinth", (52.5, 0.05, 34.0), (1.0, 0.9, 1.0), MAT_CHARCOAL)
sphere("[AnnexChroma] Sphere", (53.0, 1.75, 34.5), 0.8, MAT_WHITE, physics=False)
# single-hue rings: alternating hues within one ring hue-flips the same floor region every pass
carousels("AnnexChroma A", 53.0, 34.5, [(2.6, 2.2, 2.2)], [(1.0, 0.45, 0.15)])
carousels("AnnexChroma B", 53.0, 34.5, [(1.7, 3.4, -2.8)], [(0.3, 0.75, 1.0)])

gi_volumes("AnnexChroma", (47.5, -T, AZ0 - T), (58.5, H + T, AZ0 + AD + T))
room_probe("AnnexChroma", (53.0, H * 0.5, AZ0 + AD * 0.5), (5.0, H * 0.5, AD * 0.5), (0.0, 0.0, -3.5))
tag_folder("AnnexChroma", _start)

# =============================================================================
# hall 3: MIRROR COURT (x 50..70). Traced reflections ONLY: roughness-ramp
# floor strips 0.4 -> 0.02 toward the east (all at/below the traced-reflection
# threshold); mirror panels flank the east door; gold WILL ENGINE logo on-axis
# with an orbiting warm light; cool area washes off both side walls.
# =============================================================================
_start = len(entities)
MX0, MX1 = 50.0, 70.0
box("[Mirror] South Wall", (MX0, 0.0, -T), (MX1 - MX0, H, T), MAT_WHITE)
box("[Mirror] North Wall", (MX0, 0.0, ZW), (MX1 - MX0, H, T), MAT_WHITE)
hall_ceiling("Mirror", MX0, MX1)
for i in range(8):
    box(f"[Mirror] Floor Strip {i}", (MX0 + i * 2.5, 0.0, 0.0), (2.5, 0.05, ZW), MAT_GLOSS[i], physics=False)

# mirror panels flanking the east door (west face of vestibule MV, 0.05m proud)
box("[Mirror] Mirror S", (69.87, 0.15, 1.0), (0.08, 4.7, 4.5), MAT_MIRROR, physics=False)
box("[Mirror] Mirror N", (69.87, 0.15, 10.5), (0.08, 4.7, 4.5), MAT_MIRROR, physics=False)

# brushed panels on both side walls
for i, px in enumerate([52.0, 57.0, 62.0, 67.0]):
    box(f"[Mirror] Brushed S{i}", (px, 0.4, 0.02), (2.5, 3.0, 0.1), MAT_BRUSHED, physics=False)
    box(f"[Mirror] Brushed N{i}", (px, 0.4, ZW - 0.12), (2.5, 3.0, 0.1), MAT_BRUSHED, physics=False)

# hero: gold extruded logo on a low plinth, on-axis, read from the west
box("[Mirror] Logo Plinth", (62.55, 0.05, 5.2), (0.9, 1.0, 5.6), MAT_CHARCOAL)
logo = sign3d("[Mirror] Logo", (62.9, 1.1, 8.0), "WILL ENGINE", scale=0.7, material=MAT_GOLD, anchor=ANCHOR_BOTTOM)
logo[TEXT3D]["depth"] = 0.4

orbit = light_entity("[Mirror] Orbit Light", (65.6, 1.9, 8.0))
add_sphere_light(orbit, color=(1.0, 0.75, 0.45), intensity=30.0, radius=0.15, draw_range=14.0)
add_path_mover(orbit, circle_points(63.0, 1.9, 8.0, 2.6), speed=2.0, wait_time=0.0,
               easing=EASE_LINEAR, loop_mode=LOOP_LOOP)

wash_s = light_entity("[Mirror] Wash S", (60.0, 3.5, 0.8), tuple(face_dir(0.0, 0.0, 1.0)))
add_area_light(wash_s, color=(0.7, 0.8, 1.0), intensity=35.0, half_width=3.0, half_height=1.1, draw_range=18.0)
wash_n = light_entity("[Mirror] Wash N", (60.0, 3.5, ZW - 0.8), tuple(face_dir(0.0, 0.0, -1.0)))
add_area_light(wash_n, color=(0.7, 0.8, 1.0), intensity=35.0, half_width=3.0, half_height=1.1, draw_range=18.0)

sign3d("[Mirror] Sign", (45.88, 5.0, 8.0), "MIRROR COURT", scale=0.55)
gi_volumes("Mirror", (MX0 - T, -T, -T), (MX1 + T, H + T, ZW + T))
hall_probe("Mirror", MX0, MX1, PROBE_RES_256)
tag_folder("Mirror", _start)

# =============================================================================
# hall 4: VAULT (x 74..90). Reflection probes as the exhibit: everything
# reflective here sits ABOVE the traced threshold so it reads from probes.
# Rough-metal sphere ramp 0.45 -> 0.85, plus two saturated alcoves with their
# own nested probes (smallest-volume-wins locality demo: each sphere reflects
# its alcove's color, not the hall).
# =============================================================================
_start = len(entities)
VX0, VX1 = 74.0, 90.0
box("[Vault] South Wall", (VX0, 0.0, -T), (VX1 - VX0, H, T), MAT_WHITE)
box("[Vault] North Wall", (VX0, 0.0, ZW), (VX1 - VX0, H, T), MAT_WHITE)
box("[Vault] Floor Slab", (VX0, 0.0, 0.0), (VX1 - VX0, 0.05, ZW), MAT_VAULT_FLOOR, physics=False)
hall_ceiling("Vault", VX0, VX1)

for li, lx in enumerate([78.0, 86.0]):
    e = light_entity(f"[Vault] Downlight {li}", (lx, 5.5, 8.0), tuple(face_dir(0.0, -1.0, 0.0)))
    add_area_light(e, color=(1.0, 1.0, 1.0), intensity=28.0, half_width=1.5, half_height=1.5, draw_range=20.0)

def alcove(tag, ax, mat):
    """Colored mini-room protruding from the south wall, open north face, nested probe inside."""
    box(f"[Vault] Alcove {tag} W", (ax - 0.5, 0.0, 0.0), (0.5, 3.5, 3.0), mat)
    box(f"[Vault] Alcove {tag} E", (ax + 3.5, 0.0, 0.0), (0.5, 3.5, 3.0), mat)
    box(f"[Vault] Alcove {tag} Top", (ax - 0.5, 3.0, 0.0), (4.5, 0.5, 3.0), mat)
    box(f"[Vault] Alcove {tag} Back", (ax, 0.0, 0.0), (3.5, 3.0, 0.15), mat, physics=False)
    box(f"[Vault] Alcove {tag} Plinth", (ax + 1.35, 0.05, 1.1), (0.8, 1.0, 0.8), MAT_CHARCOAL)
    sphere(f"[Vault] Alcove {tag} Sphere", (ax + 1.75, 1.6, 1.5), 0.55, MAT_PROBE_R[0], physics=False)
    # capture above the sphere (top 2.15), below the alcove ceiling (3.0)
    e = base_entity(f"[Vault] Alcove {tag} Probe", (ax + 1.75, 1.5, 1.575),
                    scale=(1.75 + PROBE_EMBED, 1.5 + PROBE_EMBED, 1.425 + PROBE_EMBED))
    add_reflection_probe(e, name_id(f"gallery_probe_alcove_{tag}"), capture_offset=(0.0, 0.9, 0.0), resolution=PROBE_RES_128)
    entities.append(e)

alcove("Red", 76.5, MAT_RED)
alcove("Cobalt", 84.0, MAT_COBALT)

# probe roughness ramp: metal spheres 0.45 -> 0.85, all probe-fed
for i in range(5):
    px = 77.0 + i * 2.5
    box(f"[Vault] Ramp Plinth {i}", (px - 0.3, 0.05, 11.7), (0.6, 0.9, 0.6), MAT_CHARCOAL)
    sphere(f"[Vault] Ramp Sphere {i}", (px, 1.4, 12.0), 0.45, MAT_PROBE_R[i], physics=False)

sign3d("[Vault] Sign", (69.88, 5.0, 8.0), "VAULT", scale=0.55)
gi_volumes("Vault", (VX0 - T, -T, -T), (VX1 + T, H + T, ZW + T))
hall_probe("Vault", VX0, VX1, PROBE_RES_256)
tag_folder("Vault", _start)

# =============================================================================
# hall 5: FOUNDRY (x 94..112). Procedural mesh zoo: 12 shapes on labeled
# plinths in two rows, lit by three overhead area panels. Exotic-shape pivots
# are best-guess centered (cheat sheet); heights get an editor eyeball pass.
# =============================================================================
_start = len(entities)
FX0, FX1 = 94.0, 112.0
box("[Foundry] South Wall", (FX0, 0.0, -T), (FX1 - FX0, H, T), MAT_WHITE)
box("[Foundry] North Wall", (FX0, 0.0, ZW), (FX1 - FX0, H, T), MAT_WHITE)
hall_ceiling("Foundry", FX0, FX1)

for li, lx in enumerate([98.5, 103.0, 107.5]):
    e = light_entity(f"[Foundry] Downlight {li}", (lx, 5.5, 8.0), tuple(face_dir(0.0, -1.0, 0.0)))
    add_area_light(e, color=(1.0, 0.98, 0.94), intensity=28.0, half_width=1.4, half_height=1.4, draw_range=20.0)

FOUNDRY_MATS = [MAT_WHITE, MAT_GOLD, MAT_WHITE, MAT_BRUSHED, MAT_WHITE, MAT_CHARCOAL]
FOUNDRY_S = [
    ("TORUS", wa.torus_params(0.5, 0.18), 1.6),
    ("TREFOIL", wa.trefoil_params(0.35, 0.1), 1.65),
    ("KLEIN", wa.klein_params(0.45, 16, 16), 1.5),
    ("ICOSA", wa.icosa_params(0.55), 1.45),
    ("DODECA", wa.dodeca_params(0.55), 1.45),
    ("OCTA", wa.octa_params(0.6), 1.5),
]
FOUNDRY_N = [
    ("ARCH", wa.arch_params(1.2, 1.4, 0.4, 0.25), 0.9),
    ("CONE", wa.cone_params(0.45, 1.2), 0.9),
    ("CAPSULE", wa.capsule_params(0.3, 1.0), 1.7),
    ("PIPE", wa.pipe_params(0.45, 0.3, 1.0), 1.4),
    ("BOWL", wa.bowl_params(0.5, 0.35, 0.3), 0.9),
    ("SPIRAL", wa.spiral_params(12, 1.2, 0.6, 0.1, 0.04, 360.0, 4, True, False), 0.9),
]
for row, pz, label_dz, label_rot in ((FOUNDRY_S, 4.5, 0.54, (1.0, 0.0, 0.0, 0.0)), (FOUNDRY_N, 11.5, -0.54, FACE_SOUTH)):
    for i, (label, (fields, idx), y_off) in enumerate(row):
        px = 96.0 + i * 2.9
        box(f"[Foundry] Plinth {label}", (px - 0.5, 0.0, pz - 0.5), (1.0, 0.9, 1.0), MAT_CHARCOAL)
        e = base_entity(f"[Foundry] {label}", (px, y_off, pz))
        _mesh(e, idx, fields, FOUNDRY_MATS[i % len(FOUNDRY_MATS)])
        entities.append(e)
        sign3d(f"[Foundry] Label {label}", (px, 0.55, pz + label_dz), label, scale=0.18, rot=label_rot)

sign3d("[Foundry] Sign", (89.88, 5.0, 8.0), "FOUNDRY", scale=0.55)
gi_volumes("Foundry", (FX0 - T, -T, -T), (FX1 + T, H + T, ZW + T))
hall_probe("Foundry", FX0, FX1, PROBE_RES_128)
tag_folder("Foundry", _start)

# =============================================================================
# hall 6: COURT (x 116..138, open roof). Sun floods past the south wall;
# colonnade throws parallel shadows, relief wall catches raking sun, spline
# ribbon swoops overhead, gate frames sky toward the terrace.
# =============================================================================
_start = len(entities)
QX0, QX1 = 116.0, 138.0
box("[Court] South Wall", (QX0, 0.0, -T), (QX1 - QX0, H, T), MAT_WHITE)
box("[Court] North Wall", (QX0, 0.0, ZW), (QX1 - QX0, H, T), MAT_WHITE)

for i in range(8):
    px = 118.5 + i * 2.6
    for side, cz in (("S", 2.0), ("N", 14.0)):
        e = base_entity(f"[Court] Column {side}{i}", (px, H * 0.5, cz))
        f, ci = cylinder_params(0.3, H, slices=24)
        _mesh(e, ci, f, MAT_WHITE)
        entities.append(e)

# relief wall: alternating-depth blocks on the north wall, raking sun target
for i in range(10):
    d = 0.38 if i % 2 == 0 else 0.18
    box(f"[Court] Relief {i}", (117.5 + i * 2.0, 1.0, ZW - d), (1.8, 2.6, d), MAT_WHITE, physics=False)

# glossy red ribbon swooping over the courtyard (spline mesh, render-only)
ribbon = base_entity("[Court] Ribbon", (0.0, 0.0, 0.0))
ribbon[SPLINE] = {**spline_fields([(117.0, 4.5, 4.0), (122.5, 5.2, 10.5), (128.0, 3.8, 12.5), (133.0, 5.3, 8.0), (137.5, 4.6, 4.5)],
                                  radius=0.25, sides=12, segments_per_span=10),
                  "material": MAT_RIBBON, "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0]}
entities.append(ribbon)

sign3d("[Court] Sign", (111.88, 5.0, 8.0), "COURT", scale=0.55)
gi_volumes("Court", (QX0 - T, -T, -T), (QX1 + T, H + T, ZW + T))
hall_probe("Court", QX0, QX1, PROBE_RES_256, capture=(0.0, 0.0, -2.0))
tag_folder("Court", _start)

# =============================================================================
# hall 7: TERRACE (x 142..168, open sky, NO walls or pillars). Separation from
# COURT is the gate vestibule behind. Wide slab, 5x5 PBR sphere matrix in full
# sun, gold end card floating against sky at the far edge.
# =============================================================================
_start = len(entities)
box("[Terrace] Slab", (142.0, -T, -4.0), (26.0, T, 24.0), MAT_FLOOR)

# 5x5 PBR matrix: metallic along +Z rows, roughness along +X cols
for i in range(5):          # metallic (z)
    for j in range(5):      # roughness (x)
        px, pz = 147.0 + j * 1.5, 5.0 + i * 1.5
        box(f"[Terrace] Matrix Plinth {i}{j}", (px - 0.25, 0.0, pz - 0.25), (0.5, 0.8, 0.5), MAT_CHARCOAL)
        sphere(f"[Terrace] Matrix Sphere {i}{j}", (px, 1.15, pz), 0.35, MAT_PBR[(i, j)], physics=False)
sign3d("[Terrace] Matrix Key", (146.2, 0.9, 8.0), "METAL ^\nROUGH >", scale=0.3)

sign3d("[Terrace] Sign", (137.88, 5.0, 8.0), "TERRACE", scale=0.55)
sign3d("[Terrace] End Card", (166.5, 2.2, 8.0), "WILL ENGINE", scale=1.4, material=MAT_GOLD, anchor=ANCHOR_BOTTOM)
tag_folder("Terrace", _start)

# =============================================================================
# folders + write
# =============================================================================
for tag, ranges in FOLDER_TAGS.items():
    fid = next_id()
    for a, b in ranges:
        for e in entities[a:b]:
            if FOLDER in e:
                e[FOLDER]["folderId"] = fid
    entities.append({SCENE_FOLDER: {"folderId": fid, "name": tag, "parentFolder": 0}})

editor_camera = {"rotation": list(wa.camera_look_quat(1.0, -0.05, 0.0)), "translation": [-6.5, 2.4, 8.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "Demo Gallery", editor_camera=editor_camera)
n_probes = sum(1 for e in entities if wa.PROBE in e)
n_gi = sum(1 for e in entities if wa.LOCAL_DDGI in e)
print(f"wrote {SCENE_PATH} ({len(entities)} entities, {n_gi} GI volumes, {n_probes} probes)")
