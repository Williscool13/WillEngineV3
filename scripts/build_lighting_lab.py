#!/usr/bin/env python3
"""
Builds the GI/DI lighting stress-test lab: a grid of spatially separated,
self-contained test cells, one .wscene plus a set of supporting .wmaterial
assets. Run from repo root:  python scripts/build_lighting_lab.py

Cells are open-front 5-sided rooms sitting on a shared walkable ground plane,
viewed down a central aisle. Each cell isolates one failure mode of the
DI / SIGMA / GI / specular pipeline. Entities are named "[Tag] Part" so the
editor outliner groups a cell's pieces together alphabetically.

INCREMENT 1 (checkpoint): materials + 3 representative cells (color-bleed
Cornell, opposing-colour area lights, mirror floor) to validate scale, layout,
material appearance and area-light orientation before the remaining cells go in.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
from wscene_authoring import (
    base_entity, box_params, sphere_params, icosa_params, next_id,
    PROCEDURAL, PHYSICS, NAME, FOLDER, SCENE_FOLDER, TEXT3D,
    add_area_light, add_sphere_light, add_directional_light,
)

ROBOTO_FONT = 11302268835193496650   # assets/fonts/Roboto/Roboto.wsfont (baked FontID)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT_DIR = os.path.join(REPO, "assets", "materials")
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "lighting_lab.wscene")

FRAG_SHADER = 11262630175972558216
LIT_RESTIR = 7554520945661456859       # pbr_restir / reflective_restir lighting shader
LIT_EMISSIVE = 12304496605836952073    # emissive / somewhat_reflective lighting shader

# existing materials we reuse rather than re-author
MAT_MIRROR = 12524225612196796220      # reflective_restir (metallic 1, roughness 0)
MAT_GLOSSY = 13687812491276665160      # somewhat_reflective (metallic 1, roughness 0.305)

_SAMPLER = {"addressModeU": 0, "addressModeV": 0, "addressModeW": 0, "anisotropyEnable": 0,
            "magFilter": 1, "maxAnisotropy": 1.0, "maxLod": 1000.0, "minFilter": 1,
            "minLod": 0.0, "mipLodBias": 0.0, "mipmapMode": 1}

def write_material(mid, name, color_factor, emissive_factor, metal_rough, lighting_shader):
    body = {
        "alphaProperties": [0.5, 0.0, 0.0, 0.0],
        "colorFactor": list(color_factor),
        "colorUvTransform": [1.0, 1.0, 0.0, 0.0],
        "emissiveFactor": list(emissive_factor),
        "emissiveUvTransform": [1.0, 1.0, 0.0, 0.0],
        "fragmentShader": FRAG_SHADER,
        "id": mid,
        "lightingShader": lighting_shader,
        "metalRoughFactors": list(metal_rough),
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
    header = f"wmaterial\nversion 1 0\nid {mid}\nname {name}\nend_header\n"
    with open(os.path.join(MAT_DIR, name + ".wmaterial"), "w", encoding="utf-8") as f:
        f.write(header)
        f.write(json.dumps(body, indent=4))
    return mid

def diffuse(name, rgb):
    return write_material(next_id(), name, [rgb[0], rgb[1], rgb[2], 1.0], [0.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], LIT_RESTIR)

def metal(name, roughness):
    return write_material(next_id(), name, [0.9, 0.9, 0.9, 1.0], [0.0, 0.0, 0.0, 0.0], [1.0, roughness, 0.0, 0.0], LIT_RESTIR)

def emissive(name, rgb, strength):
    return write_material(next_id(), name, [0.0, 0.0, 0.0, 1.0], [rgb[0], rgb[1], rgb[2], strength], [0.0, 1.0, 0.0, 0.0], LIT_EMISSIVE)

# ---- author supporting materials ----
MAT = {}
MAT["grey"]        = diffuse("lab_grey", [0.35, 0.35, 0.35])
MAT["white"]       = diffuse("lab_white", [0.9, 0.9, 0.9])
MAT["red"]         = diffuse("lab_red", [0.75, 0.06, 0.06])
MAT["green"]       = diffuse("lab_green", [0.06, 0.75, 0.06])
MAT["blue"]        = diffuse("lab_blue", [0.06, 0.06, 0.75])
MAT["rough06"]     = metal("lab_metal_rough06", 0.6)
MAT["rough09"]     = metal("lab_metal_rough09", 0.9)
MAT["dark"]        = diffuse("lab_dark", [0.02, 0.02, 0.02])   # near-black, untextured (existing black.wmaterial is textured)
MAT["em_red"]      = emissive("lab_emissive_red", [1.0, 0.0, 0.0], 4.0)
MAT["em_green"]    = emissive("lab_emissive_green", [0.0, 1.0, 0.0], 4.0)
MAT["em_white"]    = emissive("lab_emissive_white", [1.0, 1.0, 1.0], 4.0)
MAT["em_bright"]   = emissive("lab_emissive_bright", [1.0, 1.0, 1.0], 60.0)
MAT["em_warm"]     = emissive("lab_emissive_warm", [1.0, 0.5, 0.15], 4.0)

# =============================================================================
# geometry helpers (corner-pivot boxes; render-only unless physics requested)
# =============================================================================
def _mesh(entity, ptype_idx, fields, material):
    entity[PROCEDURAL] = {**fields, "material": material, "modelFlags": [1.0, 1.0, 0.0, 0.0],
                          "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0], "type": ptype_idx}
    return entity

# Jolt BoxShapeSettings uses a default 0.05 convex radius and the direct-Box path does not clamp it
# (physics_system.cpp:434), so a half-extent < 0.05 aborts with "Invalid convex radius". Anything
# thinner than this is render-only -- too thin to be a meaningful collider for a rolling ball anyway.
MIN_HALF_EXTENT = 0.06

def _box_physics(entity, size, motion=0):
    hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    if min(hx, hy, hz) < MIN_HALF_EXTENT:
        return entity
    shape = {"type": 0, "halfExtents": [hx, hy, hz], "offset": [hx, hy, hz], "rotation": [1.0, 0.0, 0.0, 0.0],
             "bakedScaleX": 1.0, "bakedScaleY": 1.0, "bakedScaleZ": 1.0}
    entity[PHYSICS] = {"motionType": motion, "mass": 1.0, "friction": 0.5, "restitution": 0.0, "motionQuality": 0,
                       "layerOverride": 65535, "enhancedInternalEdgeRemoval": False, "isSensor": False, "shapes": [shape]}
    return entity

def box(entities, name, corner, size, material, physics=True, rot=(1.0, 0.0, 0.0, 0.0)):
    """Corner-pivot box spanning corner..corner+size. Collides by default (analytic Box shape).
    _box_physics assumes identity rotation + corner pivot, so only pass a non-identity `rot` with physics=False."""
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

def sphere(entities, name, center, radius, material, physics=True):
    e = base_entity(name, center)
    fields, idx = sphere_params(radius, 32, 32)
    _mesh(e, idx, fields, material)
    if physics:
        _sphere_physics(e, radius)
    entities.append(e)
    return e

def proc(entities, name, ptype_idx, fields, material, pos, scale=(1.0, 1.0, 1.0), rot=(1.0, 0.0, 0.0, 0.0)):
    """Render-only procedural shape (decoration / emissive mesh); no collider."""
    e = base_entity(name, pos, rot, scale)
    _mesh(e, ptype_idx, fields, material)
    entities.append(e)
    return e

def label(entities, name, text, pos, scale=0.9, material=0, rot=(0.0, 0.0, 1.0, 0.0)):
    """Render-only floating Text3D sign. Default rot = 180 about Y so the glyph face points -Z
    toward the camera; align/anchor centre it on `pos`."""
    e = base_entity(name, pos, rot)
    e[TEXT3D] = {"depth": 0.05, "flatness": 0.0005, "fontId": ROBOTO_FONT, "material": material,
                 "modelFlags": [1.0, 1.0, 0.0, 0.0], "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0],
                 "scale": scale, "smoothNormals": True, "text": text, "tracking": 0.05, "align": 1, "anchor": 2}
    entities.append(e)
    return e

# [w,x,y,z] quat mapping local +Z to a world dir. Both an area light's quad NORMAL
# (ComputeAreaLightQuadMatrix: normal = rot[2], light_components.cpp:189) and the sun's
# direction (rot*(0,0,1)) are the entity's local +Z, so this orients both.
def face_dir(dx, dy, dz):
    ax, ay, az = 0.0, 0.0, 1.0
    dot = ax * dx + ay * dy + az * dz
    if dot < -0.999999:                  # opposite -> 180 deg about Y
        return [0.0, 0.0, 1.0, 0.0]
    cx, cy, cz = ay * dz - az * dy, az * dx - ax * dz, ax * dy - ay * dx
    w = 1.0 + dot
    n = math.sqrt(w * w + cx * cx + cy * cy + cz * cz)
    return [w / n, cx / n, cy / n, cz / n]

# =============================================================================
# cell: open-front 5-sided room. Front opening faces -Z (toward the aisle/camera).
# interior spans (cx-W/2 .. cx+W/2, 0 .. H, cz-D/2 .. cz+D/2)
# =============================================================================
WALL_T = 0.15

def cell_shell(entities, tag, cx, cz, W, H, D, mat_floor, mat_wall, mat_ceiling=None, mat_left=None, mat_right=None, mat_back=None,
               floor_physics=True, close_front=False, open_top=False):
    """Open-front (toward -Z) 5-sided room. z-fight-free: full-footprint floor/ceiling slabs;
    walls sit BETWEEN them (y in [T, H-T]); side walls stop short of the back so no two exterior
    faces are coplanar-and-coincident. Every wall exterior plane is owned by exactly one box."""
    if mat_ceiling is None:
        mat_ceiling = mat_wall
    if mat_left is None:
        mat_left = mat_wall
    if mat_right is None:
        mat_right = mat_wall
    if mat_back is None:
        mat_back = mat_wall
    x0, z0 = cx - W * 0.5, cz - D * 0.5
    T = WALL_T
    box(entities, f"[{tag}] Floor", (x0, 0.0, z0), (W, T, D), mat_floor, physics=floor_physics)
    if not open_top:
        box(entities, f"[{tag}] Ceiling", (x0, H - T, z0), (W, T, D), mat_ceiling)
    box(entities, f"[{tag}] Back", (x0, T, z0 + D - T), (W, H - 2 * T, T), mat_back)
    box(entities, f"[{tag}] Left", (x0, T, z0), (T, H - 2 * T, D - T), mat_left)
    box(entities, f"[{tag}] Right", (x0 + W - T, T, z0), (T, H - 2 * T, D - T), mat_right)
    if close_front:
        box(entities, f"[{tag}] Front", (x0 + T, T, z0), (W - 2 * T, H - 2 * T, T), mat_wall)

def light_entity(entities, name, pos, rot=(1.0, 0.0, 0.0, 0.0)):
    e = base_entity(name, pos, rot)
    entities.append(e)
    return e

# =============================================================================
# build
# =============================================================================
DOWN = tuple(face_dir(0.0, -1.0, 0.0))       # ceiling area light: normal -Y
LR = 9.0                                     # default light range: contains a cell, doesn't reach neighbours

def pole(entities, name, cx, base_y, cz, height, thickness, material):
    box(entities, name, (cx - thickness * 0.5, base_y, cz - thickness * 0.5), (thickness, height, thickness), material)

def front_with_doorway(entities, tag, cx, cz, W, H, D, material, door_w=1.6, door_h=2.4):
    """Close the -Z front except for a central doorway (interior-width segments; no wall coplanarity)."""
    T = WALL_T
    x0, z0 = cx - W * 0.5, cz - D * 0.5
    side = (W - 2 * T - door_w) * 0.5
    box(entities, f"[{tag}] Front L", (x0 + T, T, z0), (side, H - 2 * T, T), material)
    box(entities, f"[{tag}] Front R", (x0 + W - T - side, T, z0), (side, H - 2 * T, T), material)
    box(entities, f"[{tag}] Lintel", (x0 + T + side, door_h, z0), (door_w, (H - T) - door_h, T), material)

# =============================================================================
# cell builders -- each takes (entities, cx, cz) and builds a self-contained test
# =============================================================================
def c_bleed(e, cx, cz):
    """GI: colour bleed. Red/green side walls, neutral light -> bounce colour accuracy."""
    cell_shell(e, "Bleed", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["white"], mat_left=MAT["red"], mat_right=MAT["green"])
    add_sphere_light(light_entity(e, "[Bleed] Light", (cx, 3.4, cz)), intensity=40, radius=0.15, draw_range=LR)

def c_opposing(e, cx, cz):
    """DI: opposing-colour area lights + centre pillar -> competing selection + shadow cross-contamination."""
    cell_shell(e, "Opposing", cx, cz, 6, 4, 6, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    box(e, "[Opposing] Pillar", (cx - 0.15, WALL_T, cz - 0.6), (0.3, 4 - 2 * WALL_T, 1.2), MAT["white"])
    la = light_entity(e, "[Opposing] Red Light", (cx - 3 + WALL_T + 0.06, 2.0, cz), tuple(face_dir(1.0, 0.0, 0.0)))
    add_area_light(la, color=(1.0, 0.1, 0.1), intensity=60, half_width=1.2, half_height=1.3, draw_range=LR)
    lb = light_entity(e, "[Opposing] Blue Light", (cx + 3 - WALL_T - 0.06, 2.0, cz), tuple(face_dir(-1.0, 0.0, 0.0)))
    add_area_light(lb, color=(0.1, 0.1, 1.0), intensity=60, half_width=1.2, half_height=1.3, draw_range=LR)

def c_mirror(e, cx, cz):
    """Specular: mirror floor + chrome ball -> lobe narrowness / reflection."""
    cell_shell(e, "Mirror", cx, cz, 6, 4, 6, mat_floor=MAT_MIRROR, mat_wall=MAT["white"])
    sphere(e, "[Mirror] Chrome Ball", (cx, 0.9, cz), 0.8, MAT_MIRROR)
    add_sphere_light(light_entity(e, "[Mirror] Light", (cx, 3.4, cz)), intensity=50, radius=0.1, draw_range=LR)

# ---- SIGMA / shadows ----
def c_penumbra(e, cx, cz):
    """SIGMA: small vs large area light, identical occluder/distance -> penumbra-width scaling."""
    W, H, D = 10, 4, 6
    cell_shell(e, "Penumbra", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    box(e, "[Penumbra] Divider", (cx - 0.075, WALL_T, cz - D * 0.5 + WALL_T), (0.15, H - 2 * WALL_T, D - 2 * WALL_T), MAT["grey"])
    pole(e, "[Penumbra] Post S", cx - W * 0.25, WALL_T, cz, 1.2, 0.3, MAT["grey"])
    pole(e, "[Penumbra] Post L", cx + W * 0.25, WALL_T, cz, 1.2, 0.3, MAT["grey"])
    add_area_light(light_entity(e, "[Penumbra] Small Light", (cx - W * 0.25, H - 0.5, cz), DOWN), intensity=120, half_width=0.12, half_height=0.12, draw_range=LR)
    add_area_light(light_entity(e, "[Penumbra] Large Light", (cx + W * 0.25, H - 0.5, cz), DOWN), intensity=120, half_width=1.4, half_height=1.4, draw_range=LR)

def c_contact(e, cx, cz):
    """SIGMA: same occluder near vs far from receiver -> contact hardening."""
    W, H, D = 6, 5, 6
    cell_shell(e, "Contact", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    box(e, "[Contact] Bar Low", (cx - 1.6, WALL_T + 0.35, cz - 1.4), (3.2, 0.1, 0.25), MAT["grey"])
    box(e, "[Contact] Bar High", (cx - 1.6, WALL_T + 2.6, cz + 1.1), (3.2, 0.1, 0.25), MAT["grey"])
    add_area_light(light_entity(e, "[Contact] Light", (cx, H - 0.5, cz), DOWN), intensity=120, half_width=0.7, half_height=0.7, draw_range=LR)

def c_overlap(e, cx, cz):
    """SIGMA: two separate lights, one occluder -> two overlapping penumbras (denoiser cross-contamination)."""
    cell_shell(e, "Overlap", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    pole(e, "[Overlap] Pole", cx, WALL_T, cz, 2.6, 0.12, MAT["grey"])
    add_sphere_light(light_entity(e, "[Overlap] Light A", (cx - 1.7, 3.4, cz - 0.4)), intensity=35, radius=0.15, draw_range=LR)
    add_sphere_light(light_entity(e, "[Overlap] Light B", (cx + 1.7, 3.4, cz + 0.4)), intensity=35, radius=0.15, draw_range=LR)

def c_grazing(e, cx, cz):
    """SIGMA: thin pole, light low at a grazing angle -> long-shadow undersampling / aliasing."""
    cell_shell(e, "Grazing", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    pole(e, "[Grazing] Pole", cx + 0.5, WALL_T, cz + 1.0, 3.2, 0.07, MAT["grey"])
    add_sphere_light(light_entity(e, "[Grazing] Light", (cx - 2.3, 0.6, cz + 1.0)), intensity=28, radius=0.08, draw_range=LR)

# ---- GI ----
def c_cornell_tall(e, cx, cz):
    """GI: tall-thin Cornell -> bounce distance vs cascade cell size (vertical)."""
    cell_shell(e, "Tall", cx, cz, 2.6, 7.5, 2.6, mat_floor=MAT["white"], mat_wall=MAT["white"], mat_left=MAT["red"], mat_right=MAT["green"])
    add_sphere_light(light_entity(e, "[Tall] Light", (cx, 6.9, cz)), intensity=45, radius=0.12, draw_range=LR)

def c_cornell_wide(e, cx, cz):
    """GI: wide-flat Cornell -> bounce distance vs cell size (horizontal)."""
    W, H, D = 9, 2.2, 9
    cell_shell(e, "Wide", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["white"], mat_left=MAT["red"], mat_right=MAT["blue"])
    add_area_light(light_entity(e, "[Wide] Light", (cx, H - 0.35, cz), DOWN), intensity=90, half_width=0.6, half_height=0.6, draw_range=LR)

def c_enclosed(e, cx, cz):
    """GI: fully enclosed room, single hidden emissive patch behind a baffle -> pure multi-bounce (no direct view of source)."""
    W, H, D = 6, 4, 6
    cell_shell(e, "Enclosed", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["white"])
    front_with_doorway(e, "Enclosed", cx, cz, W, H, D, MAT["white"])
    box(e, "[Enclosed] Emitter", (cx - 1.0, WALL_T, cz + 1.6), (2.0, 0.12, 1.2), MAT["em_bright"])
    box(e, "[Enclosed] Baffle", (cx - 1.6, WALL_T, cz - 0.2), (3.2, 2.2, 0.15), MAT["white"])

def c_corridor(e, cx, cz):
    """GI: occluded corridor, light only at the far end -> radiance-propagation lag / leak through geometry."""
    W, H, D = 3, 3, 12
    cell_shell(e, "Corridor", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    box(e, "[Corridor] Baffle A", (cx - W * 0.5 + WALL_T, WALL_T, cz - 2.0), (W * 0.62, H - 2 * WALL_T, 0.15), MAT["grey"])
    box(e, "[Corridor] Baffle B", (cx + W * 0.5 - WALL_T - W * 0.62, WALL_T, cz + 1.6), (W * 0.62, H - 2 * WALL_T, 0.15), MAT["grey"])
    add_sphere_light(light_entity(e, "[Corridor] Light", (cx, H - 0.5, cz + D * 0.5 - 1.0)), intensity=60, radius=0.15, draw_range=D + 4)

def c_crevice(e, cx, cz):
    """GI: deep parallel fins forming narrow crevices -> probe-in-geometry / backface classification stress."""
    cell_shell(e, "Crevice", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["white"])
    for i in range(4):
        box(e, f"[Crevice] Fin {i}", (cx - 1.65 + i * 1.0, WALL_T, cz - 1.0), (0.1, 2.2, 2.0), MAT["white"])
    add_sphere_light(light_entity(e, "[Crevice] Light", (cx, 3.4, cz - 1.5)), intensity=45, radius=0.12, draw_range=LR)

def c_banner(e, cx, cz):
    """GI: two-sided thin banner between a red (front) and blue (back) light -> front/back normal-key separation."""
    cell_shell(e, "Banner", cx, cz, 6, 4, 6, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    box(e, "[Banner] Banner", (cx - 1.1, WALL_T + 0.4, cz - 0.02), (2.2, 2.6, 0.04), MAT["white"])
    add_sphere_light(light_entity(e, "[Banner] Front Red", (cx, 2.0, cz - 1.8)), color=(1.0, 0.1, 0.1), intensity=35, radius=0.12, draw_range=LR)
    add_sphere_light(light_entity(e, "[Banner] Back Blue", (cx, 2.0, cz + 1.8)), color=(0.1, 0.1, 1.0), intensity=35, radius=0.12, draw_range=LR)

def c_nested(e, cx, cz):
    """GI: room-in-a-room, light in the inner room, doorway only -> light leak through geometry seams."""
    W, H, D = 9, 4, 9
    cell_shell(e, "Nested", cx, cz, W, H, D, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    # inner closed room at the back, ~4x3x4, doorway facing -Z (toward the outer opening)
    iw, ih, idp = 4.0, 3.0, 4.0
    ix, iz = cx - iw * 0.5, cz + D * 0.5 - WALL_T - idp
    T = WALL_T
    box(e, "[Nested] In Back", (ix, WALL_T, iz + idp - T), (iw, ih, T), MAT["white"])
    box(e, "[Nested] In Left", (ix, WALL_T, iz), (T, ih, idp - T), MAT["white"])
    box(e, "[Nested] In Right", (ix + iw - T, WALL_T, iz), (T, ih, idp - T), MAT["white"])
    box(e, "[Nested] In Roof", (ix, WALL_T + ih - T, iz), (iw, T, idp), MAT["white"])
    # front wall of inner room with a doorway
    side = (iw - T - 1.4) * 0.5
    box(e, "[Nested] In Front L", (ix, WALL_T, iz), (side, ih, T), MAT["white"])
    box(e, "[Nested] In Front R", (ix + iw - side, WALL_T, iz), (side, ih, T), MAT["white"])
    box(e, "[Nested] In Lintel", (ix + side, WALL_T + 2.2, iz), (iw - 2 * side, ih - 2.2, T), MAT["white"])
    add_sphere_light(light_entity(e, "[Nested] Light", (cx, WALL_T + 1.4, iz + idp * 0.5)), intensity=55, radius=0.15, draw_range=LR + 4)

# ---- DI stress ----
def c_many_emitters(e, cx, cz):
    """DI: many small emitters spanning ~4 orders of intensity -> RIS / light-selection stress, boiling."""
    cell_shell(e, "Many", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    tints = [(1, 1, 1), (1, 0.6, 0.2), (0.2, 0.5, 1), (0.3, 1, 0.3), (1, 0.3, 0.3), (0.8, 0.3, 1)]
    n = 12
    for i in range(n):
        col, row = i % 4, i // 4
        px = cx - 1.8 + col * 1.2
        py = 1.0 + row * 1.0
        pz = cz + 1.2
        inten = 0.5 * (10.0 ** (i * 4.0 / (n - 1)))   # 0.5 -> ~5000
        add_sphere_light(light_entity(e, f"[Many] Emitter {i}", (px, py, pz)), color=tints[i % len(tints)], intensity=inten, radius=0.05, draw_range=8)

def c_emissive_mesh(e, cx, cz):
    """DI: emissive mesh with wildly non-uniform triangle areas -> emissive-triangle area-sampling bias."""
    cell_shell(e, "EmMesh", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    box(e, "[EmMesh] Pedestal", (cx - 0.5, WALL_T, cz - 0.5), (1.0, 0.8, 1.0), MAT["grey"])
    fields, idx = icosa_params(1.0)
    proc(e, "[EmMesh] Emitter", idx, fields, MAT["em_white"], (cx, 1.9, cz), scale=(2.6, 0.45, 2.6))  # stretched -> tris of very different area

def c_occluder_edge(e, cx, cz):
    """DI: tiny light at a thin blade's edge -> temporal-reuse invalidation (drag the light in-editor to test)."""
    cell_shell(e, "Edge", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["grey"])
    box(e, "[Edge] Blade", (cx - 0.09, WALL_T, cz - 1.6), (0.18, 3.0, 3.0), MAT["grey"])
    add_sphere_light(light_entity(e, "[Edge] Light", (cx + 0.6, 2.7, cz + 1.0)), intensity=45, radius=0.05, draw_range=LR)

def c_coldstart(e, cx, cz):
    """DI/GI: clean neutral box, single light -> convergence-from-scratch baseline (history rejection / camera cut)."""
    cell_shell(e, "Cold", cx, cz, 6, 4, 6, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    add_sphere_light(light_entity(e, "[Cold] Light", (cx, 3.4, cz)), intensity=45, radius=0.2, draw_range=LR)

# ---- specular ----
def c_roughness_strip(e, cx, cz):
    """Specular: roughness 0 -> 0.9 metal spheres under one light -> roughness-driven specular / denoiser transition."""
    W, H, D = 9, 4, 6
    cell_shell(e, "Rough", cx, cz, W, H, D, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    mats = [MAT_MIRROR, MAT_GLOSSY, MAT["rough06"], MAT["rough09"]]
    for i, m in enumerate(mats):
        sphere(e, f"[Rough] Ball {i}", (cx - 3.0 + i * 2.0, 1.0, cz), 0.7, m)
    add_sphere_light(light_entity(e, "[Rough] Light", (cx, H - 0.4, cz)), intensity=70, radius=0.1, draw_range=LR)

def c_chrome_color(e, cx, cz):
    """Specular: chrome ball in a strongly-coloured room -> reflection colour accuracy / probe worst-case."""
    cell_shell(e, "ChromeCol", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["white"], mat_left=MAT["red"], mat_right=MAT["blue"], mat_back=MAT["green"])
    sphere(e, "[ChromeCol] Ball", (cx, 0.9, cz), 0.85, MAT_MIRROR)
    add_sphere_light(light_entity(e, "[ChromeCol] Light", (cx, 3.4, cz)), intensity=45, radius=0.12, draw_range=LR)

# ---- competing / dynamic range ----
def c_sun_vs_emissive(e, cx, cz):
    """Competing: bright warm interior emissive under the global sun -> exposure / dynamic-range fighting (raise Sun to exaggerate)."""
    cell_shell(e, "SunVsEm", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["white"])
    box(e, "[SunVsEm] Panel", (cx - 1.0, WALL_T, cz + 1.4), (2.0, 2.2, 0.2), MAT["em_bright"])

def c_ibl_twins(e, cx, cz):
    """Competing: open-top twins, IBL-only vs IBL+direct, identical geometry -> isolate sky/IBL contribution."""
    W, H, D = 8, 3, 5
    cell_shell(e, "IBL", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["grey"], open_top=True)
    box(e, "[IBL] Divider", (cx - 0.075, WALL_T, cz - D * 0.5 + WALL_T), (0.15, H - WALL_T, D - 2 * WALL_T), MAT["grey"])
    sphere(e, "[IBL] Ref L", (cx - W * 0.25, 0.7, cz), 0.5, MAT["white"])   # IBL-only half
    sphere(e, "[IBL] Ref R", (cx + W * 0.25, 0.7, cz), 0.5, MAT["white"])   # IBL + direct half
    add_sphere_light(light_entity(e, "[IBL] Direct", (cx + W * 0.25, H - 0.4, cz)), intensity=40, radius=0.12, draw_range=LR)

# ---- pathological ----
def c_firefly(e, cx, cz):
    """Pathological: near-black room, one tiny very-bright distant emitter -> fireflies / importance-sampling stress."""
    cell_shell(e, "Firefly", cx, cz, 6, 4, 6, mat_floor=MAT["dark"], mat_wall=MAT["dark"])
    add_sphere_light(light_entity(e, "[Firefly] Emitter", (cx + 2.0, 3.4, cz + 2.2)), intensity=250, radius=0.03, draw_range=LR)

def c_bounce_only(e, cx, cz):
    """Pathological: emitter tucked at the front-top facing in, no direct view from the aisle -> cache-only / bounce-lit surfaces."""
    W, H, D = 6, 4, 6
    cell_shell(e, "Bounce", cx, cz, W, H, D, mat_floor=MAT["white"], mat_wall=MAT["white"])
    box(e, "[Bounce] Soffit", (cx - W * 0.5 + WALL_T, H - 1.0, cz - D * 0.5 + WALL_T), (W - 2 * WALL_T, 0.15, 0.5), MAT["white"])
    box(e, "[Bounce] Emitter", (cx - 1.2, H - 1.3, cz - D * 0.5 + WALL_T + 0.1), (2.4, 0.15, 0.15), MAT["em_bright"])

# ---- TAA / temporal ----
def c_fence(e, cx, cz):
    """TAA: dense sub-pixel-thin fence at the back wall -> shimmer / disocclusion. Bars are far + tiny
    on purpose (that's what breaks temporal AA); render-only (too thin for a collider). Drive the light for worst case."""
    cell_shell(e, "Fence", cx, cz, 6, 4, 6, mat_floor=MAT["white"], mat_wall=MAT["white"])
    n, span, w = 34, 5.2, 0.025
    z_back = cz + 3.0 - WALL_T - 0.25   # right up against the back wall = farthest from the viewer
    for i in range(n):
        px = cx - span * 0.5 + i * (span / (n - 1))
        box(e, f"[Fence] Bar {i}", (px, WALL_T, z_back), (w, 3.4, w), MAT["grey"])
    add_sphere_light(light_entity(e, "[Fence] Light", (cx, 3.2, cz - 1.6)), intensity=55, radius=0.12, draw_range=LR)

def c_rotating_emissive(e, cx, cz):
    """TAA: emissive object on a stand, static camera -> rotate it in-editor to isolate ghosting (no camera-motion confound)."""
    cell_shell(e, "Rotate", cx, cz, 6, 4, 6, mat_floor=MAT["grey"], mat_wall=MAT["grey"])
    box(e, "[Rotate] Pedestal", (cx - 0.4, WALL_T, cz - 0.4), (0.8, 1.0, 0.8), MAT["grey"])
    fields, idx = icosa_params(1.0)
    proc(e, "[Rotate] Emitter", idx, fields, MAT["em_white"], (cx, 1.9, cz), scale=(0.7, 0.7, 0.7))

# =============================================================================
# place cells on a grid, grouped by stress-group. Each cell = a Text3D label +
# a 2-level folder (group folder -> cell folder); all a cell's entities carry
# that cell folder's id so the outliner nests them.
# =============================================================================
CELLS = [
    ("DI", "Bleed", c_bleed), ("DI", "Opposing", c_opposing), ("DI", "Many", c_many_emitters),
    ("DI", "EmMesh", c_emissive_mesh), ("DI", "Edge", c_occluder_edge), ("DI", "Cold", c_coldstart),
    ("Shadows", "Penumbra", c_penumbra), ("Shadows", "Contact", c_contact), ("Shadows", "Overlap", c_overlap), ("Shadows", "Grazing", c_grazing),
    ("GI", "Tall", c_cornell_tall), ("GI", "Wide", c_cornell_wide),
    ("GI", "Enclosed", c_enclosed), ("GI", "Corridor", c_corridor), ("GI", "Crevice", c_crevice), ("GI", "Banner", c_banner), ("GI", "Nested", c_nested),
    ("Specular", "Mirror", c_mirror), ("Specular", "Rough", c_roughness_strip), ("Specular", "ChromeCol", c_chrome_color),
    ("Competing", "SunVsEm", c_sun_vs_emissive), ("Competing", "IBL", c_ibl_twins),
    ("Pathological", "Firefly", c_firefly), ("Pathological", "Bounce", c_bounce_only),
    ("TAA", "Fence", c_fence), ("TAA", "Rotate", c_rotating_emissive),
]
COLS, PITCH_X, PITCH_Z = 6, 14.0, 16.0

entities = []
folders = []
placed = []
group_fid = {}
groups_order = []
for i, (group, tag, builder) in enumerate(CELLS):
    col, row = i % COLS, i // COLS
    cx = (col - (COLS - 1) * 0.5) * PITCH_X
    cz = row * PITCH_Z
    start = len(entities)
    builder(entities, cx, cz)
    label(entities, f"[{tag}] Label", tag, (cx, 8.5, cz))
    if group not in group_fid:
        group_fid[group] = next_id()
        groups_order.append(group)
    cell_fid = next_id()
    folders.append({SCENE_FOLDER: {"folderId": cell_fid, "name": tag, "parentFolder": group_fid[group]}})
    for ent in entities[start:]:
        ent[FOLDER]["folderId"] = cell_fid
    placed.append((cx, cz))

for g in groups_order:
    folders.append({SCENE_FOLDER: {"folderId": group_fid[g], "name": g, "parentFolder": 0}})

# shared walkable ground sized to cover the whole grid
gx0 = min(p[0] for p in placed) - 10.0
gx1 = max(p[0] for p in placed) + 10.0
gz0 = min(p[1] for p in placed) - 12.0
gz1 = max(p[1] for p in placed) + 12.0
box(entities, "Ground", (gx0, -0.52, gz0), (gx1 - gx0, 0.5, gz1 - gz0), MAT["grey"], physics=True)

# dim sun (user can disable). Direction = local +Z aimed down and forward.
_d = (0.35, -0.85, 0.4)
_dl = math.sqrt(sum(c * c for c in _d))
sun = base_entity("Sun", (0.0, 20.0, -10.0), tuple(face_dir(_d[0] / _dl, _d[1] / _dl, _d[2] / _dl)))
add_directional_light(sun, color=(1.0, 0.97, 0.9), intensity=0.5, priority=0, angular_radius_deg=1.0)
entities.append(sun)

entities.extend(folders)

# editor_camera rotation is [w,x,y,z] (same as entities, per scene_system.cpp:233/320).
# 180 deg about Y = turn around to look +Z, staying upright (cells open toward -Z).
editor_camera = {"rotation": [0.0, 0.0, 1.0, 0.0], "translation": [0.0, 6.0, -20.0]}
scene_id = next_id()
wa.write_scene(SCENE_PATH, entities, scene_id, "Lighting Lab", editor_camera=editor_camera)
print(f"wrote {SCENE_PATH} ({len(entities)} entities, {len(CELLS)} cells, {len(folders)} folders)")
print(f"wrote materials to {MAT_DIR}")
