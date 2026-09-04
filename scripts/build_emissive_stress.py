#!/usr/bin/env python3
"""
Builds the three emissive stress scenes. Run from repo root:  python scripts/build_emissive_stress.py

The emissive triangle-light build is view independent: the clear pass covers the whole
TriLightStore watermark and the build pass covers every visible emissive instance, neither
of them frustum culled. So the three costs cannot be isolated by flying between zones of one
scene, and each axis gets its own scene instead.

  emissive_stress_groups.wscene     many emissive instances, few triangles each
      1100 emissive boxes against MAX_EMISSIVE_GROUPS = 1024. Measures the per-instance
      workgroup dispatch and the work-list upload, and exercises the fill-time cap: 76 boxes
      should stay dark and InstanceStore should log the cap warning exactly once.

      Spacing and intensity are solved against the world grid rather than chosen. A group's AABB
      is inflated by its largest triangle range, R = rangeMultiplier * sqrt(intensity * area),
      and world_grid_binning keeps only MAX_EMISSIVE_GROUPS_PER_WORLD_GRID_CELL = 16 per cell.
      For a single-layer lattice of spacing d, a cell of size c carries about ((c + 2R)/d + 1)^2
      groups, so staying under 16 needs (c + 2R)/d <= 3. Cascade k has c = 2 * 2^k out to
      32 * 2^k metres, so the constraint tightens with distance and the far field always caps;
      generation prints the per-cascade prediction. An earlier layout (intensity 20, 4m spacing,
      five stacked layers) gave R = 38m against a 32m cascade 0, so every cell held every box.

      Tiers exist because a single shared power leaves the binning replacement branch dead: an
      over-subscribed cell would keep the first 16 in group order rather than the brightest.

  emissive_stress_triangles.wscene  few emissive instances, many triangles each
      8 spheres at 8192 triangles each = 65536, the exact TriLightStore capacity
      (MAX_LIGHTS - MAX_ANALYTIC_LIGHTS). Measures the clear pass (sized by the watermark),
      the build pass inner loop, and the ReSTIR transform-lights dispatch, which is also
      watermark sized and runs per view.

  emissive_stress_scan.wscene       many instances, almost none emissive
      A 39^3 lattice with an 11^3 core removed, plus 8 small emissive spheres in the core.
      Measures the GatherLights discovery walk, which is O(instances) rather than O(entities):
      it reads one field out of a ~130-byte InstanceSource for every slot in every range to
      produce 8 work items.

Triangle counts (verified against the generators, not guessed):
  procedural box    12          (6 quads)
  par_shapes sphere 2*slices*stacks   (GenerateSphere -> par_shapes_create_parametric_sphere)
Procedural models are deduplicated by a hash of their params (AssetManager::LoadProceduralModel),
so identical shapes share one model and one BLAS however many entities reference them.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wscene_authoring as wa

from wscene_authoring import (
    base_entity, next_id, name_id, add_render_flags, add_static_mesh,
    add_directional_light, add_skybox, box_params, sphere_params, plane_params,
    write_scene, PROCEDURAL, SPAWN,
)
import asset_index

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_DIR = os.path.join(REPO, "assets", "scenes")
MAT_DIR = os.path.join(REPO, "assets", "materials")

FRAG_SHADER = 16532098932897623660
# SID("default_pbr_restir") and SID("default_pbr"). These were named LIT_RESTIR/LIT_EMISSIVE;
# the second one is not an emissive shader, it is just the non-ReSTIR pipeline.
LIT_PBR_RESTIR = 6423489698308471953
LIT_PBR = 14720002576866434405

# Engine caps this exercises. Kept here so the scene sizes and the numbers in the docstring
# cannot drift apart silently.
MAX_EMISSIVE_GROUPS = 1024
MAX_EMISSIVE_GROUPS_PER_CELL = 16
TRI_LIGHT_CAPACITY = 81920 - 16384   # MAX_LIGHTS - MAX_ANALYTIC_LIGHTS
WORLD_GRID_BASE_CELL_SIZE = 2.0
WORLD_GRID_RES = 16
EMISSIVE_RANGE_MULTIPLIER = 8.0      # ReSTIR.wprofile emissiveTriRangeMultiplier, current config


def tri_light_range(intensity, triangle_area):
    """Per-triangle cull range the build pass writes; the group AABB is inflated by the largest one."""
    return EMISSIVE_RANGE_MULTIPLIER * math.sqrt(intensity * triangle_area)


def groups_per_cell(cell_size, spacing, radius):
    """Groups whose inflated AABB overlaps one cell of a single-layer lattice."""
    return ((cell_size + 2.0 * radius) / spacing + 1.0) ** 2

_SAMPLER = {"addressModeU": 0, "addressModeV": 0, "addressModeW": 0, "anisotropyEnable": 0,
            "magFilter": 1, "maxAnisotropy": 1.0, "maxLod": 1000.0, "minFilter": 1,
            "minLod": 0.0, "mipLodBias": 0.0, "mipmapMode": 1}


def write_material(name, color_factor, emissive_factor, metal_rough, lighting_shader):
    mid = name_id(name)
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
    wa.write_material_file(name, mid, body, MAT_DIR)
    return mid


def mesh_only(entity, ptype_idx, fields, material):
    """Render-only procedural shape. No PhysicsBodyDesc: tens of thousands of Jolt bodies would
    dominate the frame and drown out what these scenes measure."""
    entity[PROCEDURAL] = {**fields, "material": material,
                          "renderOffset": [0.0, 0.0, 0.0], "renderRotation": [1.0, 0.0, 0.0, 0.0],
                          "type": ptype_idx}
    return entity


def emissive_mesh(name, pos, ptype_idx, fields, material):
    e = base_entity(name, pos)
    mesh_only(e, ptype_idx, fields, material)
    add_render_flags(e, emissive_light=True)
    return e


def common_lighting(entities, envmap, sun_intensity, sky_intensity, spawn_pos):
    """Dim sun and sky on purpose: these scenes are read by how much the emissives contribute."""
    sun_right, sun_up, sun_fwd, _ = wa.basis_for((0.0, 0.0, 0.0), (0.35, -1.0, 0.5))
    sun = base_entity("Sun", (0.0, 0.0, 0.0), rot=wa.mat_to_quat(sun_right, sun_up, sun_fwd))
    add_directional_light(sun, color=(1.0, 0.96, 0.9), intensity=sun_intensity, priority=1, angular_radius_deg=0.6)
    entities.append(sun)

    sky = base_entity("Skybox", (0.0, 0.0, 0.0))
    add_skybox(sky, envmap, intensity=sky_intensity, priority=1)
    entities.append(sky)

    spawn = base_entity("PlayerSpawn", spawn_pos)
    spawn[SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 1}
    entities.append(spawn)


# =============================================================================
# A. group cap: many emissive instances, 12 triangles each
# =============================================================================
GROUP_COUNT = 1100        # deliberately over MAX_EMISSIVE_GROUPS so the cap and its warning fire
GROUP_COLS = 34
GROUP_ROWS = 33           # 1122 slots, one layer: stacking in Y would cube the per-cell count
GROUP_SPACING = 9.0
GROUP_BOX = 1.5
GROUP_BOX_TRIS = 12
GROUP_BOX_TRI_AREA = GROUP_BOX * GROUP_BOX * 0.5   # each face is two triangles
GROUP_HEIGHT = 1.5
# Eight intensity tiers, cycled. One shared power would leave the binning replacement branch dead
# and reduce every over-subscribed cell to "keep the first 16 in group order"; distinct powers make
# the survivors the brightest, which is the behaviour worth testing.
GROUP_TIERS = 8
GROUP_INTENSITY_MIN = 0.60
GROUP_INTENSITY_MAX = 0.95


def group_tier_intensity(tier):
    return GROUP_INTENSITY_MIN + (GROUP_INTENSITY_MAX - GROUP_INTENSITY_MIN) * tier / float(GROUP_TIERS - 1)


def build_groups(idx):
    wa.seed_ids("emissive_stress_groups")
    mat_floor = write_material("emstress_floor", [0.35, 0.35, 0.35, 1.0], [0.0, 0.0, 0.0, 0.0],
                               [0.0, 0.9, 0.0, 0.0], LIT_PBR_RESTIR)

    # Colours are normalised to a peak channel of 1.0 so intensity is exactly emissiveFactor.w:
    # the build pass computes intensity = w * max(rgb).
    tier_materials = []
    for tier in range(GROUP_TIERS):
        hue = tier / float(GROUP_TIERS)
        rgb = [0.55 + 0.45 * math.cos(2.0 * math.pi * (hue + phase)) for phase in (0.0, 1.0 / 3.0, 2.0 / 3.0)]
        peak = max(rgb)
        rgb = [c / peak for c in rgb]
        tier_materials.append(write_material(
            "emstress_box_t{}".format(tier), [0.0, 0.0, 0.0, 1.0],
            [rgb[0], rgb[1], rgb[2], group_tier_intensity(tier)], [0.0, 1.0, 0.0, 0.0], LIT_PBR))

    entities = []
    span_x = (GROUP_COLS - 1) * GROUP_SPACING
    span_z = (GROUP_ROWS - 1) * GROUP_SPACING

    placed = 0
    for rz in range(GROUP_ROWS):
        for cx in range(GROUP_COLS):
            if placed >= GROUP_COUNT:
                break
            # Boxes use a corner pivot, so translation is the min corner.
            pos = (cx * GROUP_SPACING - span_x * 0.5,
                   GROUP_HEIGHT,
                   rz * GROUP_SPACING - span_z * 0.5)
            fields, ptype = box_params(GROUP_BOX, GROUP_BOX, GROUP_BOX)
            entities.append(emissive_mesh("em_box_{:04d}".format(placed), pos, ptype, fields,
                                          tier_materials[placed % GROUP_TIERS]))
            placed += 1

    floor = base_entity("Floor", (0.0, 0.0, 0.0))
    fields, ptype = plane_params(span_x + 40.0, span_z + 40.0, 1, 1)
    mesh_only(floor, ptype, fields, mat_floor)
    entities.append(floor)

    # Spawn inside the field rather than outside it: the near field is what bins correctly.
    common_lighting(entities, idx.envmap("kloofendal_48d_partly_cloudy_puresky_4k"),
                    sun_intensity=0.4, sky_intensity=0.05, spawn_pos=(0.0, 1.0, 0.0))

    camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 16.0, 55.0]}
    path = os.path.join(SCENE_DIR, "emissive_stress_groups.wscene")
    write_scene(path, entities, name_id("emissive_stress_groups"), "emissive_stress_groups", editor_camera=camera)

    lit = min(GROUP_COUNT, MAX_EMISSIVE_GROUPS)
    r_min = tri_light_range(group_tier_intensity(0), GROUP_BOX_TRI_AREA)
    r_max = tri_light_range(group_tier_intensity(GROUP_TIERS - 1), GROUP_BOX_TRI_AREA)
    notes = [
        "emissive instances {:,} placed, {:,} expected to light, {:,} refused at the cap".format(
            GROUP_COUNT, lit, GROUP_COUNT - lit),
        "emissive triangles   {:,} requested, {:,} expected in the store".format(
            GROUP_COUNT * GROUP_BOX_TRIS, lit * GROUP_BOX_TRIS),
        "field {:.0f}m x {:.0f}m, spacing {:.0f}m, cull range {:.1f}-{:.1f}m over {} tiers".format(
            span_x, span_z, GROUP_SPACING, r_min, r_max, GROUP_TIERS),
        "expect exactly one 'Emissive instance cap' warning on load",
    ]
    for cascade in range(5):
        cell = WORLD_GRID_BASE_CELL_SIZE * (2 ** cascade)
        reach = cell * WORLD_GRID_RES
        occupancy = groups_per_cell(cell, GROUP_SPACING, r_max)
        verdict = "ok" if occupancy <= MAX_EMISSIVE_GROUPS_PER_CELL else "CAPS"
        notes.append("  cascade {} ({:.0f}m cells, to {:.0f}m): ~{:.0f} groups/cell of {} {}".format(
            cascade, cell, reach, occupancy, MAX_EMISSIVE_GROUPS_PER_CELL, verdict))
    return path, notes


# =============================================================================
# B. watermark: few emissive instances, many triangles each
# =============================================================================
TRI_SPHERE_SLICES = 64
TRI_SPHERE_STACKS = 64
TRI_SPHERE_TRIS = 2 * TRI_SPHERE_SLICES * TRI_SPHERE_STACKS   # 8192
TRI_SPHERE_COUNT = TRI_LIGHT_CAPACITY // TRI_SPHERE_TRIS      # 8, saturating the store exactly
TRI_SPHERE_RADIUS = 3.0
TRI_RING_RADIUS = 18.0


def build_triangles(idx):
    wa.seed_ids("emissive_stress_triangles")
    mat_floor = write_material("emstress_floor_b", [0.35, 0.35, 0.35, 1.0], [0.0, 0.0, 0.0, 0.0],
                               [0.0, 0.9, 0.0, 0.0], LIT_PBR_RESTIR)

    # One material per sphere so a single material edit cannot relight all of them at once, which
    # is what the light-up staleness path would need to be tested against later.
    entities = []
    for i in range(TRI_SPHERE_COUNT):
        a = (i / float(TRI_SPHERE_COUNT)) * 2.0 * math.pi
        hue = i / float(TRI_SPHERE_COUNT)
        rgb = (0.5 + 0.5 * math.cos(2.0 * math.pi * hue),
               0.5 + 0.5 * math.cos(2.0 * math.pi * (hue + 1.0 / 3.0)),
               0.5 + 0.5 * math.cos(2.0 * math.pi * (hue + 2.0 / 3.0)))
        mat = write_material("emstress_sphere_{}".format(i), [0.0, 0.0, 0.0, 1.0],
                             [rgb[0], rgb[1], rgb[2], 12.0], [0.0, 1.0, 0.0, 0.0], LIT_PBR)
        pos = (math.cos(a) * TRI_RING_RADIUS, TRI_SPHERE_RADIUS + 1.0, math.sin(a) * TRI_RING_RADIUS)
        fields, ptype = sphere_params(TRI_SPHERE_RADIUS, TRI_SPHERE_SLICES, TRI_SPHERE_STACKS)
        entities.append(emissive_mesh("em_sphere_{}".format(i), pos, ptype, fields, mat))

    floor = base_entity("Floor", (0.0, 0.0, 0.0))
    fields, ptype = plane_params(TRI_RING_RADIUS * 4.0, TRI_RING_RADIUS * 4.0, 1, 1)
    mesh_only(floor, ptype, fields, mat_floor)
    entities.append(floor)

    common_lighting(entities, idx.envmap("kloofendal_48d_partly_cloudy_puresky_4k"),
                    sun_intensity=0.4, sky_intensity=0.05, spawn_pos=(0.0, 1.0, 0.0))

    camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 10.0, TRI_RING_RADIUS + 26.0]}
    path = os.path.join(SCENE_DIR, "emissive_stress_triangles.wscene")
    write_scene(path, entities, name_id("emissive_stress_triangles"), "emissive_stress_triangles", editor_camera=camera)

    total = TRI_SPHERE_COUNT * TRI_SPHERE_TRIS
    return path, [
        "emissive instances {}, {:,} triangles each".format(TRI_SPHERE_COUNT, TRI_SPHERE_TRIS),
        "emissive triangles {:,} of {:,} TriLightStore capacity ({:.0f}%)".format(
            total, TRI_LIGHT_CAPACITY, 100.0 * total / TRI_LIGHT_CAPACITY),
        "a 9th sphere would be refused whole: TriLightStore ranges are all or nothing",
    ]


# =============================================================================
# C. discovery scan: many instances, almost none emissive
# =============================================================================
SCAN_GRID = 39            # odd, so the void centres on a lattice cell
SCAN_VOID = 11
SCAN_SPACING = 6.0
SCAN_EMISSIVE_COUNT = 8
SCAN_SPHERE_SLICES = 16
SCAN_SPHERE_STACKS = 16
SCAN_SPHERE_TRIS = 2 * SCAN_SPHERE_SLICES * SCAN_SPHERE_STACKS   # 512

SCAN_CENTRE = (SCAN_GRID - 1) // 2
SCAN_VOID_LO = SCAN_CENTRE - (SCAN_VOID - 1) // 2
SCAN_VOID_HI = SCAN_CENTRE + (SCAN_VOID - 1) // 2
SCAN_BOX_COUNT = SCAN_GRID ** 3 - SCAN_VOID ** 3
SCAN_VOID_HALF = ((SCAN_VOID - 1) / 2.0) * SCAN_SPACING


def build_scan(idx):
    wa.seed_ids("emissive_stress_scan")
    box_model = idx.model("BoxTextured4k.glb")
    mat_em = write_material("emstress_scan_emissive", [0.0, 0.0, 0.0, 1.0], [0.6, 0.85, 1.0, 30.0],
                            [0.0, 1.0, 0.0, 0.0], LIT_PBR)

    entities = []
    for ix in range(SCAN_GRID):
        x = (ix - SCAN_CENTRE) * SCAN_SPACING
        in_void_x = SCAN_VOID_LO <= ix <= SCAN_VOID_HI
        for iy in range(SCAN_GRID):
            y = (iy - SCAN_CENTRE) * SCAN_SPACING
            in_void_xy = in_void_x and SCAN_VOID_LO <= iy <= SCAN_VOID_HI
            for iz in range(SCAN_GRID):
                if in_void_xy and SCAN_VOID_LO <= iz <= SCAN_VOID_HI:
                    continue
                z = (iz - SCAN_CENTRE) * SCAN_SPACING
                e = base_entity("box_{}_{}_{}".format(ix, iy, iz), (x, y, z))
                add_static_mesh(e, box_model)
                entities.append(e)

    assert len(entities) == SCAN_BOX_COUNT, (len(entities), SCAN_BOX_COUNT)

    ring = SCAN_VOID_HALF - 8.0
    for i in range(SCAN_EMISSIVE_COUNT):
        a = (i / float(SCAN_EMISSIVE_COUNT)) * 2.0 * math.pi
        pos = (math.cos(a) * ring, 0.0, math.sin(a) * ring)
        fields, ptype = sphere_params(1.5, SCAN_SPHERE_SLICES, SCAN_SPHERE_STACKS)
        entities.append(emissive_mesh("em_scan_{}".format(i), pos, ptype, fields, mat_em))

    common_lighting(entities, idx.envmap("kloofendal_48d_partly_cloudy_puresky_4k"),
                    sun_intensity=0.4, sky_intensity=0.05, spawn_pos=(0.0, 1.0, 0.0))

    camera = {"rotation": [1.0, 0.0, 0.0, 0.0], "translation": [0.0, 3.0, SCAN_VOID_HALF - 4.0]}
    path = os.path.join(SCENE_DIR, "emissive_stress_scan.wscene")
    write_scene(path, entities, name_id("emissive_stress_scan"), "emissive_stress_scan", editor_camera=camera)

    return path, [
        "instances {:,} total, {} emissive ({:.3f}%)".format(
            len(entities), SCAN_EMISSIVE_COUNT, 100.0 * SCAN_EMISSIVE_COUNT / len(entities)),
        "lattice {g}^3 with a {v}^3 core removed, spacing {s:.0f}u".format(
            g=SCAN_GRID, v=SCAN_VOID, s=SCAN_SPACING),
        "emissive triangles {:,}, negligible against the {:,} store".format(
            SCAN_EMISSIVE_COUNT * SCAN_SPHERE_TRIS, TRI_LIGHT_CAPACITY),
    ]


def main():
    idx = asset_index.scan()
    for build in (build_groups, build_triangles, build_scan):
        path, notes = build(idx)
        size_mb = os.path.getsize(path) / (1024.0 * 1024.0)
        print("wrote {}  ({:.1f} MB)".format(path, size_mb))
        for note in notes:
            print("  " + note)
        print("")


if __name__ == "__main__":
    main()
