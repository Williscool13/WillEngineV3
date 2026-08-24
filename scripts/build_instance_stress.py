#!/usr/bin/env python3
"""
Builds the instance stress scene: assets/scenes/instance_stress.wscene
Run from repo root:  python scripts/build_instance_stress.py

A 47x47x47 lattice of BoxTextured4k instances with the central 13x13x13 block carved out,
leaving 101,626 boxes around a hollow core. Every box is the same model, so the whole scene
is ONE BLAS with ~101k TLAS instances sharing ONE material (the 4096x4096 wood texture the
model already owns). That is the point: it isolates per-instance cost from geometry and
material variety.

What it is for:
  - per-frame upload cost at ~100k instances (SetupUniforms zones: Instances / Models)
  - TLAS build + RT traversal against a very large, very sparse instance count
  - graph setup / compile / execute scaling

No physics bodies anywhere: 101k Jolt bodies would dominate the frame and drown out what
this scene is meant to measure. Boxes are StaticMesh only.

Engine caps this scene needs (raised 2026-08-19 to make it loadable):
  MAX_MODEL_SLOTS    16384 -> 131072   (model_store.h)
  MAX_INSTANCE_SLOTS 65536 -> 131072   (model_interop.h)
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
wa.seed_ids("instance_stress")   # must precede every next_id() call

from wscene_authoring import (
    base_entity, next_id, name_id, add_static_mesh, add_world_text,
    add_directional_light, add_skybox, add_procedural, box_params, write_scene,
    ALIGN_CENTER, ANCHOR_CENTER, SPAWN, PROCEDURAL,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "instance_stress.wscene")
SCENE_ID = name_id("instance_stress")

# 47 and 13 are both odd so the void centres exactly on the lattice centre cell (index 23).
GRID = 47
VOID = 13
SPACING = 6.0

CENTRE = (GRID - 1) // 2          # 23
VOID_LO = CENTRE - (VOID - 1) // 2  # 17
VOID_HI = CENTRE + (VOID - 1) // 2  # 29

BOX_COUNT = GRID ** 3 - VOID ** 3
EXTENT = (GRID - 1) * SPACING       # 276u corner to corner
VOID_HALF = ((VOID - 1) / 2.0) * SPACING  # 36u from centre to last empty cell centre

# Player pad. Runtime builds boot straight into play, so the ball needs somewhere to land.
# Player is a sphere of radius 0.5 (physics_character.h), spawned at the PlayerSpawn transform.
PAD_SIZE = 16.0     # square, centred on the origin
PAD_THICK = 1.0     # deck sits below y=0 so the walkable surface is exactly y=0
LIP_HEIGHT = 0.75   # shallow: above ball radius so a slow roll is caught, low enough to see over
LIP_THICK = 0.5
SPAWN_HEIGHT = 1.0  # ball centre; drops 0.5 onto the deck


def main():
    idx = asset_index.scan()
    box_model = idx.model("BoxTextured4k.glb")
    font = idx.font("Roboto")
    text_mat = idx.text_material("default_diegetic")
    envmap = idx.envmap("kloofendal_48d_partly_cloudy_puresky_4k")
    pad_mat = idx.material("pbr_wood_floor")

    entities = []

    # ---- the lattice ----
    for ix in range(GRID):
        x = (ix - CENTRE) * SPACING
        in_void_x = VOID_LO <= ix <= VOID_HI
        for iy in range(GRID):
            y = (iy - CENTRE) * SPACING
            in_void_xy = in_void_x and VOID_LO <= iy <= VOID_HI
            for iz in range(GRID):
                if in_void_xy and VOID_LO <= iz <= VOID_HI:
                    continue
                z = (iz - CENTRE) * SPACING
                e = base_entity("box_{}_{}_{}".format(ix, iy, iz), (x, y, z))
                add_static_mesh(e, box_model)
                entities.append(e)

    assert len(entities) == BOX_COUNT, (len(entities), BOX_COUNT)

    # ---- signage in the hollow core, one panel per horizontal face, all facing inward ----
    # Text lies in the entity's local XY plane facing +Z, so each panel just needs a yaw.
    panel_dist = VOID_HALF - 6.0
    R = math.sqrt(0.5)
    panels = [
        ("south", (0.0, 0.0, -panel_dist), (1.0, 0.0, 0.0, 0.0)),   # faces +Z
        ("north", (0.0, 0.0, panel_dist), (0.0, 0.0, 1.0, 0.0)),    # 180 about Y, faces -Z
        ("west", (-panel_dist, 0.0, 0.0), (R, 0.0, R, 0.0)),        # +90 about Y, faces +X
        ("east", (panel_dist, 0.0, 0.0), (R, 0.0, -R, 0.0)),        # -90 about Y, faces -X
    ]

    headline = "{:,} BOXES".format(BOX_COUNT)
    detail = "\n".join([
        "{g} x {g} x {g} lattice, {v}^3 core removed".format(g=GRID, v=VOID),
        "lattice: 1 BLAS, 1 material, 4096x4096 wood",
        "spacing {:.0f}u   extent {:.0f}u   core {:.0f}u".format(SPACING, EXTENT, VOID_HALF * 2.0),
        "no physics on the lattice; only the pad collides",
    ])

    for tag, pos, rot in panels:
        head = base_entity("sign_{}_title".format(tag), (pos[0], pos[1] + 6.0, pos[2]), rot=rot)
        add_world_text(head, headline, font, text_material_id=text_mat, scale=3.0,
                       color=(1.0, 0.85, 0.45, 1.0), align=ALIGN_CENTER, anchor=ANCHOR_CENTER)
        entities.append(head)

        body = base_entity("sign_{}_body".format(tag), (pos[0], pos[1] + 1.5, pos[2]), rot=rot)
        add_world_text(body, detail, font, text_material_id=text_mat, scale=1.1,
                       color=(0.85, 0.9, 1.0, 1.0), align=ALIGN_CENTER, anchor=ANCHOR_CENTER)
        entities.append(body)

    # ---- player pad: procedural boxes use a CORNER pivot, so position = min corner ----
    half = PAD_SIZE / 2.0

    def solid(name, corner, size):
        e = base_entity(name, corner)
        fields, ptype = box_params(*size)
        add_procedural(e, ptype, fields, motion=0, friction=0.6)
        e[PROCEDURAL]["material"] = pad_mat
        entities.append(e)

    solid("pad_deck", (-half, -PAD_THICK, -half), (PAD_SIZE, PAD_THICK, PAD_SIZE))
    solid("pad_lip_south", (-half, 0.0, -half), (PAD_SIZE, LIP_HEIGHT, LIP_THICK))
    solid("pad_lip_north", (-half, 0.0, half - LIP_THICK), (PAD_SIZE, LIP_HEIGHT, LIP_THICK))
    solid("pad_lip_west", (-half, 0.0, -half + LIP_THICK), (LIP_THICK, LIP_HEIGHT, PAD_SIZE - 2.0 * LIP_THICK))
    solid("pad_lip_east", (half - LIP_THICK, 0.0, -half + LIP_THICK), (LIP_THICK, LIP_HEIGHT, PAD_SIZE - 2.0 * LIP_THICK))

    # ---- lighting ----
    # Sun direction is the entity's local +Z, so build a basis whose fwd is the direction we want.
    sun_right, sun_up, sun_fwd, _ = wa.basis_for((0.0, 0.0, 0.0), (0.35, -1.0, 0.5))
    sun = base_entity("Sun", (0.0, 0.0, 0.0), rot=wa.mat_to_quat(sun_right, sun_up, sun_fwd))
    add_directional_light(sun, color=(1.0, 0.96, 0.9), intensity=4.0, priority=1, angular_radius_deg=0.6)
    entities.append(sun)

    sky = base_entity("Skybox", (0.0, 0.0, 0.0))
    add_skybox(sky, envmap, intensity=1.0, priority=1)
    entities.append(sky)

    spawn = base_entity("PlayerSpawn", (0.0, SPAWN_HEIGHT, 0.0))
    spawn[SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 1}
    entities.append(spawn)

    camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 3.0, 20.0]}
    write_scene(SCENE_PATH, entities, SCENE_ID, "instance_stress", editor_camera=camera)

    size_mb = os.path.getsize(SCENE_PATH) / (1024.0 * 1024.0)
    print("wrote {}".format(SCENE_PATH))
    print("  entities {:,}  (boxes {:,})".format(len(entities), BOX_COUNT))
    print("  lattice  {g}^3 spacing {s}  extent {e:.0f}u  core {c:.0f}u".format(
        g=GRID, s=SPACING, e=EXTENT, c=VOID_HALF * 2.0))
    print("  pad      {p:.0f}x{p:.0f} deck top y=0, {h:.2f}u lip, spawn y={s:.1f}".format(
        p=PAD_SIZE, h=LIP_HEIGHT, s=SPAWN_HEIGHT))
    print("  size     {:.1f} MB".format(size_mb))


if __name__ == "__main__":
    main()
