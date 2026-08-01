#!/usr/bin/env python3
"""
Declares the Poly Haven PBR sets (assets/textures/src/<set>/) as texture stubs and writes one
textured material per set. Run from repo root: python scripts/build_pbr_materials.py
The engine generates the stubs in place on next editor startup; materials reference the declared
ids so the order does not matter.

Formats: albedo BC7 sRGB; normal BC5 (shader reconstructs Z from RG, nor_gl Y+ matches the glTF
convention the importer uses); roughness BC7 linear (grayscale replicates into G, which is the
channel SampleMetalRoughLod multiplies roughness by; metallic factor 0 neutralizes B).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa

SETS = ["red_brick", "blue_plaster_wall", "wood_floor", "green_metal_rust", "clay_roof_tiles", "checkered_pavement_tiles"]

for s in SETS:
    diff = wa.write_texture_stub(f"{s}_diff", f"src/{s}/{s}_diff_1k.png", wa.DXGI_BC7_UNORM_SRGB)
    nor = wa.write_texture_stub(f"{s}_nor", f"src/{s}/{s}_nor_1k.png", wa.DXGI_BC5_UNORM)
    rough = wa.write_texture_stub(f"{s}_rough", f"src/{s}/{s}_rough_1k.png", wa.DXGI_BC7_UNORM)
    mid = wa.write_material(f"pbr_{s}", albedo_tex=diff, metal_rough_tex=rough, normal_tex=nor, metallic=0.0, roughness=1.0)
    print(f"pbr_{s}: material {mid:x}, textures {diff:x}/{nor:x}/{rough:x}")
