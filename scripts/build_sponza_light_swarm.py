#!/usr/bin/env python3
"""
Sponza (classic) with a swarm of sphere lights ping-ponging across the atrium at speed.
Every light is a PathMover, so nothing moves until Play. Evening sky, no sun.
Run from repo root:  python scripts/build_sponza_light_swarm.py
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("sponza_light_swarm")

from wscene_authoring import base_entity, name_id, add_sphere_light, add_path_mover, add_static_mesh, add_skybox, add_procedural, add_render_flags, box_params, EASE_LINEAR, LOOP_PINGPONG
import asset_index

IDX = asset_index.scan()
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "scenes", "sponza_light_swarm.wscene")
SCENE_ID = name_id("sponza_light_swarm")

# Sponza.gltf bounds at its 0.008 root scale: x -15.4..14.4, y -1.0..11.4, z -9.5..8.8; atrium floor y 0, gallery y 4
X0, X1 = -12.0, 11.0
Z0, Z1 = -4.0, 4.0
Y0, Y1 = 1.5, 9.5
NITS = 65536.0
rng = random.Random(0x5107)

COLORS = [(1.0, 0.25, 0.1), (0.1, 0.5, 1.0), (0.2, 1.0, 0.3), (1.0, 0.85, 0.2), (1.0, 0.2, 0.8), (0.2, 1.0, 1.0), (1.0, 1.0, 1.0)]

entities = []

sponza = base_entity("Sponza", (0.0, 0.0, 0.0))
add_static_mesh(sponza, IDX.model("Sponza.gltf"))
entities.append(sponza)

sky = base_entity("Sky", (0.0, 0.0, 0.0))
add_skybox(sky, IDX.envmap("modern_evening_street_4k"), intensity=0.0)
entities.append(sky)

# Sponza has no collision; an invisible slab with its top at the atrium floor carries the ball
floor = base_entity("Floor", (-20.0, -0.5, -12.0))
fields, ptype = box_params(40.0, 0.5, 24.0)
add_procedural(floor, ptype, fields)
add_render_flags(floor, visible=False, probe_bake_include=False, ddgi_contribute=False)
entities.append(floor)

spawn = base_entity("Spawn", (8.0, 1.0, 0.0))
spawn[wa.SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 1}
entities.append(spawn)

def swarm_light(name, a, b, color):
    # Random phase so the swarm does not move in lockstep
    t = rng.random()
    e = base_entity(name, tuple(a[i] + (b[i] - a[i]) * t for i in range(3)))
    add_sphere_light(e, color=color, intensity=rng.uniform(240.0, 600.0) * NITS, radius=0.06, draw_range=18.0)
    # PathMover speed is segments per second; convert from m/s
    length = math.sqrt(sum((b[i] - a[i]) ** 2 for i in range(3)))
    add_path_mover(e, [a, b], speed=rng.uniform(1.5, 3.0) / length, wait_time=0.0, easing=EASE_LINEAR, loop_mode=LOOP_PINGPONG)
    e[wa.PATH_MOVER]["progress"] = t
    entities.append(e)

lights = 0
# Along the atrium
for i in range(160):
    y = rng.uniform(Y0, Y1)
    z = rng.uniform(Z0, Z1)
    a, b = (X0, y, z), (X1, y, z)
    if rng.random() < 0.5: a, b = b, a
    swarm_light(f"Swarm X {i}", a, b, COLORS[lights % len(COLORS)])
    lights += 1
# Across it
for i in range(48):
    x = rng.uniform(X0, X1)
    y = rng.uniform(Y0, Y1)
    a, b = (x, y, Z0), (x, y, Z1)
    if rng.random() < 0.5: a, b = b, a
    swarm_light(f"Swarm Z {i}", a, b, COLORS[lights % len(COLORS)])
    lights += 1
# Up and down
for i in range(48):
    x = rng.uniform(X0, X1)
    z = rng.uniform(Z0, Z1)
    a, b = (x, Y0, z), (x, Y1, z)
    if rng.random() < 0.5: a, b = b, a
    swarm_light(f"Swarm Y {i}", a, b, COLORS[lights % len(COLORS)])
    lights += 1

editor_camera = {"rotation": list(wa.camera_look_quat(-1.0, -0.12, 0.0)), "translation": [12.5, 4.5, 0.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "Sponza Light Swarm", editor_camera=editor_camera)
print(f"wrote {SCENE_PATH}: {len(entities)} entities, {lights} lights")
