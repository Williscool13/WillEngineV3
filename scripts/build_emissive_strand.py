#!/usr/bin/env python3
"""
Emissive-group granularity A/B. Run from repo root:  python scripts/build_emissive_strand.py

Two scenes with IDENTICAL geometry, materials, lighting and camera: a wall with a strand of
32 small emissive bulbs 2cm off its face.

  emissive_strand_split.wscene    32 procedural sphere entities  -> 32 EmissiveGroups
  emissive_strand_merged.wscene   one ModuleMeshComponent, 32 parts on slot 0 -> ONE primitive, ONE EmissiveGroup
  emissive_strand_spheres.wscene  32 analytic sphere lights, same radius/radiance -> cone (solid-angle) sampled, no triangles

If the wall boils in merged and is clean in split, the boil is WorldGridProposal's uniform-within-group
triangle draw over a spatially large group (the bistro string-light case). RESULT 2026-08-30: split and merged look the same, so it is not.
spheres isolates the emitter sampling instead: emissive triangles are area-sampled (pdf 1/area, half of a
bulb's triangles backfacing), sphere lights are cone-sampled. Clean spheres = near-field triangle sampling is the noise.

Sun is off and the sky is dim so the wall reads the bulbs only.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa
from wscene_authoring import base_entity, name_id, add_render_flags, add_module, module_part, add_sphere_light, box_params, sphere_params, plane_params, write_scene
from build_emissive_stress import write_material, mesh_only, emissive_mesh, common_lighting, LIT_PBR_RESTIR
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_DIR = os.path.join(REPO, "assets", "scenes")

BULB_COUNT = 32           # add_module part cap
BULB_RADIUS = 0.02
BULB_SLICES = 8
BULB_STACKS = 8           # 2*8*8 = 128 triangles per bulb, 4096 merged
BULB_SPACING = 0.3        # 9.3m strand
BULB_GAP = 0.02           # bulb surface to wall face
BULB_Y = 2.0
BULB_RADIANCE = 40.0      # emissiveFactor.w; sphere-light `intensity` is the same radiance unit (EvalPHat: area * intensity)
WALL_SIZE = (12.0, 3.0, 0.2)
WALL_CORNER = (-1.0, 0.0, -WALL_SIZE[2])   # front face on z = 0
ENVMAP = "kloofendal_48d_partly_cloudy_puresky_4k"
CAMERA = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [4.65, 1.9, 2.2]}
SPAWN_POS = (4.65, 0.0, 3.0)


def bulb_offsets():
    z = BULB_GAP + BULB_RADIUS
    return [(i * BULB_SPACING, BULB_Y, z) for i in range(BULB_COUNT)]


def static_geometry(entities, mat_wall, mat_floor):
    wall = base_entity("Wall", WALL_CORNER)
    mesh_only(wall, box_params(*WALL_SIZE)[1], box_params(*WALL_SIZE)[0], mat_wall)
    entities.append(wall)
    floor = base_entity("Floor", (4.65, 0.0, 4.0))
    mesh_only(floor, plane_params(24.0, 16.0)[1], plane_params(24.0, 16.0)[0], mat_floor)
    entities.append(floor)


def build(idx, variant):
    wa.seed_ids("emissive_strand_" + variant)
    mat_wall = write_material("emstrand_wall", [0.6, 0.6, 0.6, 1.0], [0.0, 0.0, 0.0, 0.0], [0.0, 0.9, 0.0, 0.0], LIT_PBR_RESTIR)
    mat_floor = write_material("emstrand_floor", [0.3, 0.3, 0.3, 1.0], [0.0, 0.0, 0.0, 0.0], [0.0, 0.9, 0.0, 0.0], LIT_PBR_RESTIR)
    mat_bulb = write_material("emstrand_bulb", [0.0, 0.0, 0.0, 1.0], [1.0, 0.8, 0.55, BULB_RADIANCE], [0.0, 0.5, 0.0, 0.0], LIT_PBR_RESTIR)

    entities = []
    static_geometry(entities, mat_wall, mat_floor)
    shape = sphere_params(BULB_RADIUS, BULB_SLICES, BULB_STACKS)
    if variant == "split":
        for i, off in enumerate(bulb_offsets()):
            entities.append(emissive_mesh("Bulb {:02d}".format(i), off, shape[1], shape[0], mat_bulb))
    elif variant == "spheres":
        for i, off in enumerate(bulb_offsets()):
            e = base_entity("Bulb {:02d}".format(i), off)
            add_sphere_light(e, color=(1.0, 0.8, 0.55), intensity=BULB_RADIANCE, radius=BULB_RADIUS, draw_range=4.0)
            entities.append(e)
    else:
        strand = base_entity("Strand", (0.0, 0.0, 0.0))
        add_module(strand, [module_part(shape, offset=off, slot=0) for off in bulb_offsets()], materials=[mat_bulb])
        add_render_flags(strand, emissive_light=True)
        entities.append(strand)

    common_lighting(entities, idx.envmap(ENVMAP), sun_intensity=0.0, sky_intensity=0.02, spawn_pos=SPAWN_POS)
    name = "emissive_strand_" + variant
    path = os.path.join(SCENE_DIR, name + ".wscene")
    write_scene(path, entities, name_id(name), name, editor_camera=CAMERA)
    return path


def main():
    idx = asset_index.scan()
    for variant in ("split", "merged", "spheres"):
        print("wrote " + build(idx, variant))


if __name__ == "__main__":
    main()
