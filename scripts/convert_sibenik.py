#!/usr/bin/env python3
"""
Converts assets/models/SibenikChurch/sibenik.obj to a glTF the engine importer accepts.
Runs inside headless Blender, not the system python:

    blender --background --factory-startup --python scripts/convert_sibenik.py -- [options]

The source OBJ needs four fixups before it is usable, all of them unrecoverable
after export (see notes on each stage):
  1. it carries ZERO vertex normals, so they must be generated
  2. its two "bump" maps are greyscale HEIGHT maps, and the engine reads a normal
     map as BC5 two-channel with Z reconstructed; wiring them up produces garbage
  3. the stained glass is Kd 0,0,0 with an illum 4/6 Tf transmission filter that
     glTF cannot express, so the windows arrive as black opaque panels that seal
     the building
  4. the engine importer does not generate tangents (it writes a constant
     {1,0,0,1} when TANGENT is absent), so normal maps must stay off

Options (after the -- separator):
  --glass {emissive,delete,keep}  window treatment, default emissive
  --glass-strength FLOAT          emission strength for --glass emissive, default 40
  --smooth-angle DEGREES          auto-smooth threshold, default 35
  --out PATH                      output .glb, default alongside the .obj
"""
import argparse
import math
import os
import sys

import bpy

SRC = os.path.join("assets", "models", "SibenikChurch", "sibenik.obj")

# Tf transmission filter per the .mtl; these are the actual stained-glass colours,
# and the only place the window tint survives the OBJ.
GLASS = {
    "staklo": (0.5, 0.5, 0.5),
    "staklo_crveno": (0.882353, 0.207843, 0.098039),
    "staklo_zeleno": (0.066667, 0.631373, 0.074510),
    "staklo_plavo": (0.082353, 0.145098, 0.784314),
    "staklo_zuto": (1.000000, 0.890196, 0.007843),
}


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--glass", choices=["emissive", "delete", "keep"], default="emissive")
    p.add_argument("--glass-strength", type=float, default=40.0)
    p.add_argument("--smooth-angle", type=float, default=35.0)
    p.add_argument("--out", default=None)
    return p.parse_args(argv)


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def principled(mat):
    if not mat.use_nodes:
        return None
    for node in mat.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return None


def strip_bump(mat):
    """Drops the height-map bump chain. Blender wires map_bump into a Bump node, and
    anything reaching the Normal socket risks the exporter emitting a normalTexture."""
    node = principled(mat)
    if node is None:
        return
    socket = node.inputs["Normal"]
    for link in list(socket.links):
        mat.node_tree.links.remove(link)
    for n in list(mat.node_tree.nodes):
        if n.type in {"BUMP", "NORMAL_MAP"} or (n.type == "TEX_IMAGE" and n.image and "bump" in n.image.name.lower()):
            mat.node_tree.nodes.remove(n)


def make_emissive(mat, colour, strength):
    node = principled(mat)
    if node is None:
        return
    node.inputs["Emission Color"].default_value = (*colour, 1.0)
    node.inputs["Emission Strength"].default_value = strength
    node.inputs["Base Color"].default_value = (0.0, 0.0, 0.0, 1.0)


def delete_glass_faces(names):
    import bmesh
    removed = 0
    for obj in [o for o in bpy.data.objects if o.type == "MESH"]:
        slots = {i for i, s in enumerate(obj.material_slots) if s.material and s.material.name in names}
        if not slots:
            continue
        mesh = bmesh.new()
        mesh.from_mesh(obj.data)
        doomed = [f for f in mesh.faces if f.material_index in slots]
        removed += len(doomed)
        bmesh.ops.delete(mesh, geom=doomed, context="FACES")
        mesh.to_mesh(obj.data)
        mesh.free()
        obj.data.update()
    return removed


def main():
    args = parse_args()
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(repo, SRC)
    if not os.path.isfile(src):
        sys.exit("missing source: " + src)
    out = args.out or os.path.join(os.path.dirname(src), "sibenik.glb")

    clear_scene()
    bpy.ops.wm.obj_import(filepath=src, forward_axis="NEGATIVE_Z", up_axis="Y")

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    if not meshes:
        sys.exit("import produced no meshes")

    # The OBJ has no vn records at all, so every normal here is synthesised.
    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(args.smooth_angle))
        obj.select_set(False)

    glass_names = set()
    for mat in bpy.data.materials:
        strip_bump(mat)
        node = principled(mat)
        if node is not None:
            node.inputs["Metallic"].default_value = 0.0
            # The OBJ importer turns "Tf 1 1 1" into full transmission and "Ks 0 0 0" into
            # zero specular, so untouched stone exports as transmissive with ior 1 (the
            # engine reads ior into physicalProperties.x, killing Fresnel).
            node.inputs["Transmission Weight"].default_value = 0.0
            node.inputs["Specular IOR Level"].default_value = 0.5
            node.inputs["IOR"].default_value = 1.5
        base = mat.name.split(".")[0]
        if base in GLASS:
            glass_names.add(mat.name)
            if args.glass == "emissive":
                make_emissive(mat, GLASS[base], args.glass_strength)

    removed = delete_glass_faces(glass_names) if args.glass == "delete" else 0

    bpy.ops.export_scene.gltf(
        filepath=out,
        export_format="GLB",
        export_yup=True,
        export_apply=True,
        export_normals=True,
        export_tangents=False,
        export_materials="EXPORT",
        export_animations=False,
        export_cameras=False,
        export_lights=False,
    )

    tris = sum(len(o.data.loop_triangles) for o in meshes for _ in [o.data.calc_loop_triangles()])
    print("[convert_sibenik] out={} meshes={} materials={} glass={} removed_faces={} tris={}".format(
        out, len(meshes), len(bpy.data.materials), args.glass, removed, tris))


if __name__ == "__main__":
    main()
