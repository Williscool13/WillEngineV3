#!/usr/bin/env python3
"""
Builds the reflection-probe face-orientation validation room: a sealed 8m cube
whose six inner walls are emissive in a distinct axis color, each carrying a
Text3D axis label. Run from repo root:  python scripts/build_probe_orientation_room.py

Purpose (checkpoint 3d): bake a probe at the room center and inspect the six
captured/assembled faces. Colors identify which wall landed on which cube
layer; the text labels detect mirror flips (mirrored glyphs) and quarter-turn
rotations (tilted baselines) that colors alone cannot.

Color key:            Expected label reading orientation:
  +X red                +X/-X/+Z/-Z: upright, world +Y up
  -X magenta            +Y (ceiling): "up" of the glyphs points world +Z
  +Y green              -Y (floor):   "up" of the glyphs points world -Z
  -Y yellow
  +Z blue
  -Z cyan

Emissive walls make the test lighting-independent; no sun, no sky exposure.

WARNING: the live assets/scenes/probe_orientation_room.wscene has been edited in the
editor since this script last wrote it -- it carries a hand-placed Probe entity (whose
probeId names a baked .wprobe) and three hand-made view spheres. RE-RUNNING THIS SCRIPT
DISCARDS THOSE. Only re-run if you mean to go back to the generated state.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa

wa.seed_ids("probe_orientation_room")   # must precede every next_id() call; see seed_ids()

from wscene_authoring import base_entity, box_params, sphere_params, name_id, PROCEDURAL, TEXT3D

MAT_MIRROR = 12524225612196796220   # reflective_restir (metallic 1, roughness 0)
MAT_GLOSSY = 13687812491276665160   # somewhat_reflective (metallic 1, roughness 0.305)

ROBOTO_FONT = 11302268835193496650
FRAG_SHADER = 11262630175972558216
LIT_EMISSIVE = 12304496605836952073

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT_DIR = os.path.join(REPO, "assets", "materials")
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "probe_orientation_room.wscene")
# Pinned: leaving this to the tail of the next_id() sequence changed the scene's identity
# every time an entity was added ahead of it.
SCENE_ID = 92882415086882491

_SAMPLER = {"addressModeU": 0, "addressModeV": 0, "addressModeW": 0, "anisotropyEnable": 0,
            "magFilter": 1, "maxAnisotropy": 1.0, "maxLod": 1000.0, "minFilter": 1,
            "minLod": 0.0, "mipLodBias": 0.0, "mipmapMode": 1}

def emissive_material(name, rgb, strength):
    # id derives from the material's own (unique) file name -- NOT next_id(), which handed these
    # the same ids as build_lighting_lab.py's first 7 materials, so one set silently replaced the
    # other in the asset manager and the lab rendered with these emissives on its walls.
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
    import json
    header = f"wmaterial\nversion 1 0\nid {mid}\nname {name}\nend_header\n"
    with open(os.path.join(MAT_DIR, name + ".wmaterial"), "w", encoding="utf-8") as f:
        f.write(header)
        f.write(json.dumps(body, indent=4))
    return mid

MAT = {
    "+X": emissive_material("probe_room_px_red",     [1.0, 0.0, 0.0], 3.0),
    "-X": emissive_material("probe_room_nx_magenta", [1.0, 0.0, 1.0], 3.0),
    "+Y": emissive_material("probe_room_py_green",   [0.0, 1.0, 0.0], 3.0),
    "-Y": emissive_material("probe_room_ny_yellow",  [1.0, 1.0, 0.0], 3.0),
    "+Z": emissive_material("probe_room_pz_blue",    [0.0, 0.0, 1.0], 3.0),
    "-Z": emissive_material("probe_room_nz_cyan",    [0.0, 1.0, 1.0], 3.0),
    "label": emissive_material("probe_room_label_white", [1.0, 1.0, 1.0], 6.0),
}

entities = []

def wall(name, corner, size, mat):
    e = base_entity(name, corner)
    fields, idx = box_params(size[0], size[1], size[2])
    e[PROCEDURAL] = {**fields, "material": mat, "modelFlags": [1.0, 1.0, 0.0, 0.0],
                     "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0], "type": idx}
    entities.append(e)

# Interior 8m cube: -4..4 on all axes. Walls 0.3 thick, corner-pivot boxes.
# Floor/ceiling span the full footprint and Z walls span the full X range so
# every edge overlaps; no sky can leak in.
H = 4.0
T = 0.3
wall("Wall +X red",     ( H,     -H, -H),     (T, 2 * H, 2 * H),         MAT["+X"])
wall("Wall -X magenta", (-H - T, -H, -H),     (T, 2 * H, 2 * H),         MAT["-X"])
wall("Wall +Y green",   (-H - T,  H, -H - T), (2 * (H + T), T, 2 * (H + T)), MAT["+Y"])
wall("Wall -Y yellow",  (-H - T, -H - T, -H - T), (2 * (H + T), T, 2 * (H + T)), MAT["-Y"])
wall("Wall +Z blue",    (-H - T, -H,  H),     (2 * (H + T), 2 * H, T),   MAT["+Z"])
wall("Wall -Z cyan",    (-H - T, -H, -H - T), (2 * (H + T), 2 * H, T),   MAT["-Z"])

def label(name, text, pos, rot):
    e = base_entity(name, pos, rot)
    e[TEXT3D] = {"depth": 0.05, "flatness": 0.0005, "fontId": ROBOTO_FONT, "material": MAT["label"],
                 "modelFlags": [1.0, 1.0, 0.0, 0.0], "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0],
                 "scale": 1.4, "smoothNormals": True, "text": text, "tracking": 0.05, "align": 1, "anchor": 2}
    entities.append(e)

# Quats are [w,x,y,z]. Identity leaves the glyph face pointing +Z.
S = 0.70710678
IN = 3.6  # labels inset from the walls
label("Label +X", "+X", ( IN, 0.0, 0.0), ( S, 0.0, -S, 0.0))   # -90 about Y, faces -X
label("Label -X", "-X", (-IN, 0.0, 0.0), ( S, 0.0,  S, 0.0))   # +90 about Y, faces +X
label("Label +Z", "+Z", (0.0, 0.0,  IN), (0.0, 0.0, 1.0, 0.0)) # 180 about Y, faces -Z
label("Label -Z", "-Z", (0.0, 0.0, -IN), (1.0, 0.0, 0.0, 0.0)) # identity, faces +Z
label("Label +Y", "+Y", (0.0,  IN, 0.0), ( S,  S, 0.0, 0.0))   # +90 about X, faces -Y, glyph-up = +Z
label("Label -Y", "-Y", (0.0, -IN, 0.0), ( S, -S, 0.0, 0.0))   # -90 about X, faces +Y, glyph-up = -Z

# Viewing spheres: excluded from the probe bake (modelFlags.y = 0) so the capture
# stays walls-only, but visible live to inspect the probe's reflections on a
# mirror and at a mip-blurring roughness. Place the probe entity at the origin.
def view_sphere(name, pos, mat):
    e = base_entity(name, pos)
    fields, idx = sphere_params(1.0, 32, 32)
    e[PROCEDURAL] = {**fields, "material": mat, "modelFlags": [1.0, 0.0, 0.0, 0.0],
                     "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0], "type": idx}
    entities.append(e)

view_sphere("View Sphere Mirror", (-1.5, -2.8, 0.0), MAT_MIRROR)
view_sphere("View Sphere Glossy", ( 1.5, -2.8, 0.0), MAT_GLOSSY)

editor_camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 0.0, -2.0]}
wa.write_scene(SCENE_PATH, entities, SCENE_ID, "Probe Orientation Room", editor_camera=editor_camera)
print(f"wrote {SCENE_PATH} ({len(entities)} entities)")
print(f"wrote 7 materials to {MAT_DIR}")
