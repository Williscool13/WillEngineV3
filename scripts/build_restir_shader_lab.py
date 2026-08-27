#!/usr/bin/env python3
"""
Builds a scene that holds more than one ReSTIR lighting bucket.
Run from repo root:  python scripts/build_restir_shader_lab.py

Until now exactly one lighting shader was registered with LightingShaderType::ReSTIR
(default_pbr_restir), so in ReSTIR mode every material collapsed into a single bucket and the
per-pixel bucket guard in visibility_lighting_common never had anything to reject. This scene
pairs default_pbr_restir with default_toon_restir and interleaves them, so a guard that leaks
shows up immediately as a sphere shaded by the wrong model.

Layout: a floor, a back wall, and two rows of six spheres on pedestals. Columns alternate
material, and the rows swap the alternation, so no two neighbours in either axis share a bucket.
Lit by two sphere lights, one area panel and a sun, all of which feed ReSTIR DI.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("restir_shader_lab")

from wscene_authoring import (
    base_entity, box_params, plane_params, sphere_params,
    add_procedural, add_sphere_light, add_area_light, add_directional_light, add_skybox,
)
import asset_index

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("modern_evening_street_4k")

SCENE_PATH = os.path.join(wa._REPO_ROOT, "assets", "scenes", "restir_shader_lab.wscene")
SCENE_ID = wa.name_id("restir_shader_lab")


def face_dir(dx, dy, dz):
    """[w,x,y,z] quat mapping local +Z to (dx,dy,dz); the sun shines along its local +Z."""
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / n, dy / n, dz / n
    if dz < -0.999999:
        return [0.0, 0.0, 1.0, 0.0]
    cx, cy = -dy, dx
    w = 1.0 + dz
    m = math.sqrt(w * w + cx * cx + cy * cy)
    return [w / m, cx / m, cy / m, 0.0]

# One material per (lighting shader, tint). Same albedo per pair so the only visible difference is the shading model.
MAT_PBR = wa.write_material("restir_lab_pbr", base_color=(0.72, 0.72, 0.74, 1.0), metallic=0.0, roughness=0.55,
                            lighting_shader="default_pbr_restir")
MAT_TOON = wa.write_material("restir_lab_toon", base_color=(0.72, 0.72, 0.74, 1.0), metallic=0.0, roughness=0.55,
                             lighting_shader="default_toon_restir")
MAT_FLOOR = wa.write_material("restir_lab_floor", base_color=(0.45, 0.45, 0.48, 1.0), metallic=0.0, roughness=0.85,
                              lighting_shader="default_pbr_restir")
MAT_WALL = wa.write_material("restir_lab_wall", base_color=(0.55, 0.52, 0.48, 1.0), metallic=0.0, roughness=0.9,
                             lighting_shader="default_pbr_restir")

COLUMNS = 6
ROWS = 2
SPACING = 3.0
PEDESTAL = (1.2, 0.8, 1.2)
SPHERE_RADIUS = 0.9

entities = []

floor = base_entity("Floor", (0.0, 0.0, 0.0))
_fields, _idx = plane_params(40.0, 24.0, 8, 5)
add_procedural(floor, _idx, _fields)
floor[wa.PROCEDURAL]["material"] = MAT_FLOOR
entities.append(floor)

# Corner pivot on Box: place by its minimum corner, not its centre
wall = base_entity("BackWall", (-20.0, 0.0, -10.0))
_fields, _idx = box_params(40.0, 8.0, 0.5)
add_procedural(wall, _idx, _fields)
wall[wa.PROCEDURAL]["material"] = MAT_WALL
entities.append(wall)

x0 = -0.5 * (COLUMNS - 1) * SPACING
z0 = -0.5 * (ROWS - 1) * SPACING - 1.0
for row in range(ROWS):
    for col in range(COLUMNS):
        # Swap the alternation per row so neighbours differ along both axes
        bToon = ((row + col) % 2) == 1
        mat = MAT_TOON if bToon else MAT_PBR
        tag = "toon" if bToon else "pbr"
        x = x0 + col * SPACING
        z = z0 + row * SPACING

        pedestal = base_entity(f"Pedestal_{row}_{col}", (x - 0.5 * PEDESTAL[0], 0.0, z - 0.5 * PEDESTAL[2]))
        pfields, pidx = box_params(*PEDESTAL)
        add_procedural(pedestal, pidx, pfields)
        pedestal[wa.PROCEDURAL]["material"] = MAT_WALL
        entities.append(pedestal)

        ball = base_entity(f"Ball_{tag}_{row}_{col}", (x, PEDESTAL[1] + SPHERE_RADIUS, z))
        sfields, sidx = sphere_params(SPHERE_RADIUS, 32, 32)
        add_procedural(ball, sidx, sfields)
        ball[wa.PROCEDURAL]["material"] = mat
        entities.append(ball)

warm = base_entity("KeyLight", (-5.0, 4.5, 3.0))
add_sphere_light(warm, color=(1.0, 0.82, 0.6), intensity=140.0, radius=0.35, draw_range=28.0)
entities.append(warm)

cool = base_entity("FillLight", (5.0, 4.5, 3.0))
add_sphere_light(cool, color=(0.55, 0.7, 1.0), intensity=140.0, radius=0.35, draw_range=28.0)
entities.append(cool)

# Emissive quad lies in the local XZ plane, so an unrotated panel points straight down
panel = base_entity("CeilingPanel", (0.0, 7.0, -2.0))
add_area_light(panel, color=(1.0, 0.96, 0.9), intensity=90.0, half_width=6.0, half_height=2.0, draw_range=30.0)
entities.append(panel)

sun = base_entity("Sun", (0.0, 12.0, 0.0), rot=tuple(face_dir(-0.35, -1.0, -0.4)))
add_directional_light(sun, color=(1.0, 0.97, 0.92), intensity=1.6, priority=0, angular_radius_deg=1.0)
entities.append(sun)

sky = base_entity("Skybox", (0.0, 0.0, 0.0))
add_skybox(sky, ENV_MAP, intensity=0.25, priority=0)
entities.append(sky)

editor_camera = {"rotation": wa.camera_look_quat(0.0, -0.25, -1.0), "translation": [0.0, 5.0, 12.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "ReSTIR Shader Lab", editor_camera=editor_camera)

n_toon = sum(1 for e in entities if e.get(wa.PROCEDURAL, {}).get("material") == MAT_TOON)
n_pbr = sum(1 for e in entities if e.get(wa.PROCEDURAL, {}).get("material") == MAT_PBR)
print(f"wrote {SCENE_PATH} ({len(entities)} entities)")
print(f"buckets: {n_pbr} default_pbr_restir, {n_toon} default_toon_restir")
