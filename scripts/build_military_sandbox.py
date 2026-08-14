#!/usr/bin/env python3
"""
Builds "FOB Longbow" -- a greyboxed military sandbox laid out to exercise the
DI / GI pipeline. One .wscene plus the .wmaterial assets it needs. Run from repo root:

    python scripts/build_military_sandbox.py

Textured from the Poly Haven 1k PBR sets under assets/textures/src/ (CC0), declared as
header-only stubs the engine generates into .wtexture on next editor startup. Box / Wall /
Staircase primitives carry world-space UVs (1 UV unit = 1 m), so a material's uv_scale is
tiles-per-metre on those; curved primitives (Pipe, Cylinder caps, par_shapes surfaces) use
normalized 0..1 UVs, so the culvert bore and the helipad deck get their own materials with
per-axis uv_scale sized to the actual surface. Saturated zone colours survive as albedo
tints (colorFactor multiplies the map), so the bleed reads stay.

Why the zones are shaped the way they are -- each isolates something different:

  Hangar        Deep open-front shed; the low sun rakes in through a 14x7 door. Back wall
                is red oxide against neutral grey floor and ceiling -> the strongest
                colour-bleed read in the level. Work lights cover only the front half, so
                the back third is lit by bounce alone. Clerestory slots on both side walls
                drop narrow shafts onto the red wall, chopped into stripes by the trusses.
  Bunker        Sealed concrete box behind a blast baffle. The doorway has no direct line
                to open sky, so exterior light arrives already one bounce old; the only
                other paths in are two embrasure slits. Interior colour comes from a green
                console bank and a red warning strip playing against one white and one
                blue side wall. With GI off this room is essentially black.
  Culvert       Three surface-laid concrete pipes butted end to end, open at both mouths.
                A clean one-dimensional falloff gradient -- walk it to eyeball banding,
                leaking and temporal lag. One weak interior lamp anchors the middle so
                bounce-from-the-mouths is separable from direct falloff.
  Helipad       Twelve weak pad emitters in a ring plus a floodlit apron: many small lights
                in one place, which is the case ReSTIR light selection exists for.
  Revetment     HESCO U packed with crates and drums, one lamp inside. Tight concave
                corners -> short-range occlusion and contact darkening.
  Motor pool    Containers, chicane barriers, fuel tanks, vehicle mock-ups, tyre stacks.
                Mid-frequency geometry throwing long sun shadows across open sand.
  Radar mast    18m lattice with obstruction beacons and thin antenna whips. Small bright
                emitters at distance, plus a deliberately hostile thin-geometry shadow case.
  Watchtowers   Spiral stair, roofed deck, and a narrow aimed searchlight -- the sharpest
                DI edge in the level.

Lighting is a single low sun from the south-east, a partly-cloudy pure-sky envmap on the
same entity (sky fills what the sun does not reach), plus local emitters. The bunker and
the culvert middle still have no direct sky line, so they stay bounce-fed.

Baking the reflection probes
----------------------------
Eight probes ship unbaked (standInEnvMap 0), so they contribute nothing until you bake.
Bake with a profile that has the GI diffuse gather OFF (DDGI itself stays on) and GTAO OFF
-- both are view-dependent per face and would tint each cube face differently, and runtime
GTAO already multiplies probe content, so baked AO double-darkens corners. `ReSTIR Bake`
in config/profiles/lighting is the profile shaped for this. The bake is 2-pass for
interbounce and hijacks the main viewport; output lands in assets/probes/probe_<id>.wprobe.

Probe ids come from name_id(<probe name>), so they are stable across re-runs of this script
and a rebake overwrites in place rather than orphaning the old file. Re-running the script
after a bake keeps the bakes valid as long as the probe NAMES do not change; rename one and
its .wprobe is orphaned. Rebake after any lighting change.

Every fixture in the level is single-sourced -- either an analytic light drawing its own
proxy, or an emissive mesh, never both on the same fixture. That matters for baking: the
engine auto-hides area/sphere proxies during a bake but not ordinary emissive meshes, so a
hand-made glowing rep sitting on an analytic light would both double the light and bake in
as a blown-out blob. The bunker's screens and warning strip are the deliberate emissive-mesh
case and carry no analytic light.

Placement conventions worth remembering while editing this file:
  * Box / Wedge / Staircase / Wall are CORNER pivot; Cylinder / Pipe / Sphere / Capsule
    are CENTERED; Cone / Hemisphere / Bowl are base-at-y=0. See wscene_authoring.py:82.
  * Nothing uses transform.scale except reflection probes, where scale IS the half-extents.
    add_procedural bakes bakedScale 1.0, so a scaled mesh would not match its collider.
  * Shells follow the room_boxes() tiling rule: floor and roof take the full footprint,
    two opposite walls span the full run, the other two fill only the gap. Two coplanar
    co-visible faces z-fight, so no face plane is ever owned by more than one box.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asset_index
import wscene_authoring as wa
wa.seed_ids("military_sandbox")   # must precede every next_id() call; see seed_ids()

from wscene_authoring import (
    base_entity, add_procedural, name_id, next_id,
    box_params, cylinder_params, sphere_params, capsule_params, wedge_params,
    pipe_params, torus_params, cone_params, spiral_params, wall_params, ring_params,
    bowl_params, add_area_light, add_sphere_light, add_directional_light, add_skybox,
    add_reflection_probe, add_checkpoint, mat_to_quat, PROBE_RES_128, PROBE_RES_256,
    PROCEDURAL, NAME, FOLDER, SCENE_FOLDER, SPAWN, RENDER_DEFAULTS,
    write_material, write_texture_stub, camera_look_quat, shot, write_shots,
    DXGI_BC5_UNORM, DXGI_BC7_UNORM, DXGI_BC7_UNORM_SRGB,
)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(REPO, "assets", "scenes", "military_sandbox.wscene")
SCENE_NAME = "military_sandbox"
SCENE_ID = name_id(SCENE_NAME)

IDX = asset_index.scan()
ENV_MAP = IDX.envmap("kloofendal_48d_partly_cloudy_puresky_4k")


# =============================================================================
# materials
# =============================================================================
def pbr_set(set_name):
    """(diff, nor, arm) stub ids for the Poly Haven set at assets/textures/src/<set>/.
    arm is the packed AO/Rough/Metal map: G is the channel SampleMetalRoughLod multiplies
    roughness by and B the one metallic multiplies, so it drops straight into the MR slot.
    Stubs never overwrite an already-generated .wtexture; the declared id survives."""
    diff = write_texture_stub(f"{set_name}_diff", f"src/{set_name}/{set_name}_diff_1k.png", DXGI_BC7_UNORM_SRGB)
    nor = write_texture_stub(f"{set_name}_nor", f"src/{set_name}/{set_name}_nor_1k.png", DXGI_BC5_UNORM)
    arm = write_texture_stub(f"{set_name}_arm", f"src/{set_name}/{set_name}_arm_1k.png", DXGI_BC7_UNORM)
    return diff, nor, arm


T_SAND = pbr_set("gravelly_sand")
T_CONCRETE = pbr_set("concrete")
T_PAINTED = pbr_set("concrete_floor_painted")
T_ASPHALT = pbr_set("asphalt_02")
T_SHEET = pbr_set("rusty_metal_sheet")          # flat worn sheet: bare steel/alu and the painted-plate tints
T_CLADDING = pbr_set("box_profile_metal_sheet")  # profiled cladding: hangar roof/door wall, masts
# Pre-existing set from build_pbr_materials.py: plain rough grayscale in the MR slot
# instead of arm (metallic 0, so the stray B channel never contributes).
T_BLUE = (write_texture_stub("blue_plaster_wall_diff", "src/blue_plaster_wall/blue_plaster_wall_diff_1k.png", DXGI_BC7_UNORM_SRGB),
          write_texture_stub("blue_plaster_wall_nor", "src/blue_plaster_wall/blue_plaster_wall_nor_1k.png", DXGI_BC5_UNORM),
          write_texture_stub("blue_plaster_wall_rough", "src/blue_plaster_wall/blue_plaster_wall_rough_1k.png", DXGI_BC7_UNORM))


def textured(name, tex, uv=0.5, tint=(1.0, 1.0, 1.0), metallic=0.0, roughness=1.0):
    """uv is tiles-per-metre on world-UV primitives; pass a (u, v) tuple for the curved
    normalized-UV cases. tint multiplies the albedo map; metallic/roughness multiply the
    arm B/G channels, so a painted finish over plate is tint + metallic 0."""
    scale = uv if isinstance(uv, tuple) else (uv, uv)
    diff, nor, mr = tex
    return write_material(name, base_color=(tint[0], tint[1], tint[2], 1.0), metallic=metallic,
                          roughness=roughness, albedo_tex=diff, metal_rough_tex=mr,
                          normal_tex=nor, uv_scale=scale)


def flat(name, rgb, roughness=1.0):
    return write_material(name, base_color=(rgb[0], rgb[1], rgb[2], 1.0), roughness=roughness)


def emissive(name, rgb, strength):
    """Emissive meshes are gathered as triangle lights, so `strength` is a real light
    contribution and not just a bright pixel -- keep the reps small or they dominate."""
    return write_material(name, base_color=(0.0, 0.0, 0.0, 1.0),
                          emissive=(rgb[0], rgb[1], rgb[2], strength), lighting_shader="default_pbr")


M = {}
# ground / structure
M["sand"] = textured("mil_sand", T_SAND, uv=0.3, tint=(1.0, 0.94, 0.80))
M["concrete"] = textured("mil_concrete", T_CONCRETE, uv=0.5)
M["concrete_dark"] = textured("mil_concrete_dark", T_CONCRETE, uv=0.5, tint=(0.5, 0.5, 0.49))
M["asphalt"] = textured("mil_asphalt", T_ASPHALT, uv=0.35, tint=(0.85, 0.84, 0.82), roughness=1.15)
M["white"] = textured("mil_white", T_PAINTED, uv=0.5)
# Prefab panel for the huts and gatehouse; the bunker's white bleed wall stays T_PAINTED,
# which is genuinely light once lit (the hangar floor is the same map).
M["panel"] = textured("mil_panel", T_SHEET, uv=0.5, tint=(0.86, 0.87, 0.84))
M["floor"] = textured("mil_floor", T_PAINTED, uv=0.4, tint=(0.72, 0.72, 0.70))
# flat bright paint for markings (helipad ring/H); a photo texture there reads as dirt
M["marking"] = flat("mil_marking", (0.85, 0.85, 0.82), roughness=0.6)
# the three bleed drivers -- saturated tints, so indirect colour is unmistakable
M["red"] = textured("mil_red_oxide", T_CONCRETE, uv=0.5, tint=(1.0, 0.20, 0.13))
M["blue"] = textured("mil_blue", T_BLUE, uv=0.5, tint=(0.5, 0.6, 1.0))
M["olive"] = textured("mil_olive", T_SHEET, uv=0.5, tint=(0.35, 0.41, 0.20))
M["od_light"] = textured("mil_od_light", T_SHEET, uv=0.5, tint=(0.62, 0.64, 0.42))
M["navy"] = textured("mil_navy", T_SHEET, uv=0.5, tint=(0.14, 0.20, 0.45))
# container red is sheet-based: the concrete-based mil_red_oxide reads as wood planks on a box
M["container_red"] = textured("mil_container_red", T_SHEET, uv=0.5, tint=(0.62, 0.14, 0.10))
M["hazard"] = textured("mil_hazard", T_SHEET, uv=0.5, tint=(1.0, 0.72, 0.06))
M["rubber"] = flat("mil_rubber", (0.045, 0.045, 0.05), roughness=0.95)
# uniform fill for HESCO courses; the old per-cube colour alternation read as checkerboard once textured
M["hesco"] = textured("mil_hesco", T_SAND, uv=0.45, tint=(0.95, 0.88, 0.74))
# bare metals -- the specular / probe story (the tinted sheets above are painted, so dielectric)
M["steel"] = textured("mil_steel", T_SHEET, uv=0.5, metallic=1.0)
M["steel_rough"] = textured("mil_steel_rough", T_CLADDING, uv=0.5, tint=(0.8, 0.8, 0.81), metallic=1.0, roughness=1.4)
M["alu"] = textured("mil_alu", T_SHEET, uv=0.5, tint=(1.0, 1.0, 1.02), metallic=1.0, roughness=0.5)
# grey galvanized for structural steel: T_CLADDING's rust hue made every mast and leg read painted-red
M["galv"] = textured("mil_galv", T_SHEET, uv=0.5, tint=(0.60, 0.63, 0.66), metallic=1.0, roughness=0.75)
# curved-primitive variants: normalized 0..1 UVs, per-axis scale sized to the surface
M["concrete_pipe"] = textured("mil_concrete_pipe", T_CONCRETE, uv=(6.0, 5.5))   # u = angle over the ~12.6m bore, v = 11m segment length
M["asphalt_pad"] = textured("mil_asphalt_pad", T_ASPHALT, uv=(12.0, 12.0))      # deck cap disc spans 0..1 across 24m
# emitters -- only the bunker's, now that every other fixture is a single analytic light
# drawing its own proxy. A hand-made emissive rep sitting on top of an analytic light would
# be counted twice (the mesh is gathered as a triangle light) AND would bake into the probe
# as a blown-out blob, since only the engine's own proxies are auto-hidden during a bake.
M["em_red"] = emissive("mil_em_red", [1.0, 0.07, 0.03], 40.0)
M["em_green"] = emissive("mil_em_green", [0.14, 1.0, 0.34], 26.0)


# =============================================================================
# math / entity helpers
# =============================================================================
IDENT = (1.0, 0.0, 0.0, 0.0)
GRADE = 0.0            # top of the sand ground slab; everything sits on this


def yaw(deg):
    """Rotation about +Y: local +X -> (cos, 0, -sin), local +Z -> (sin, 0, cos)."""
    h = math.radians(deg) * 0.5
    return (math.cos(h), 0.0, math.sin(h), 0.0)


def pitch(deg):
    """Rotation about +X: local +Y -> (0, cos, sin)."""
    h = math.radians(deg) * 0.5
    return (math.cos(h), math.sin(h), 0.0, 0.0)


def roll(deg):
    """Rotation about +Z: local +Y -> (-sin, cos, 0)."""
    h = math.radians(deg) * 0.5
    return (math.cos(h), 0.0, 0.0, math.sin(h))


def face_dir(dx, dy, dz):
    """Quaternion rotating local +Z onto (dx,dy,dz). Directional-light direction and
    area-light normal are both read off local +Z, so this aims either one."""
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    dx, dy, dz = dx / n, dy / n, dz / n
    dot = dz                          # dot((0,0,1), d)
    if dot < -0.999999:
        return (0.0, 0.0, 1.0, 0.0)   # 180 about Y
    cx, cy, cz = -dy, dx, 0.0         # cross((0,0,1), d)
    w = 1.0 + dot
    m = math.sqrt(w * w + cx * cx + cy * cy + cz * cz)
    return (w / m, cx / m, cy / m, cz / m)


def run_quat(dx, dz):
    """Rotation whose local +X follows a horizontal direction, +Y stays up, and +Z is
    cross(X,Y) = (-dz, 0, dx) -- i.e. thickness falls to the LEFT of travel."""
    n = math.hypot(dx, dz)
    ux, uz = dx / n, dz / n
    return mat_to_quat((ux, 0.0, uz), (0.0, 1.0, 0.0), (-uz, 0.0, ux))


def qmul(a, b):
    """Hamilton product a*b, [w,x,y,z]: b applied first, then a."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def yaw_offset(px, pz, deg, dx, dz):
    """World XZ of a corner-pivot local offset (dx, dz) under yaw(deg) about (px, pz).
    Attachments computed in unrotated coords come apart when each part then yaw-rotates
    about its OWN corner; route every sub-part position through this instead."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return px + dx * c + dz * s, pz - dx * s + dz * c


entities = []
folders = []


def shape(entities, name, pos, params, material, rot=IDENT, folder=0, motion=0, friction=0.5):
    """Any procedural primitive plus a mesh-backed collider. Uses add_procedural's Collider
    shape rather than an analytic Box/Sphere so a rotated entity still collides where it
    renders."""
    fields, idx = params
    e = base_entity(name, pos, rot, folder_id=folder)
    add_procedural(e, idx, fields, motion=motion, friction=friction)
    e[PROCEDURAL]["material"] = material
    entities.append(e)
    return e


def visual(entities, name, pos, params, material, rot=IDENT, folder=0):
    """Render-only: no PhysicsBodyDesc at all. For light reps, markings and trim that
    should not thicken the collision world."""
    fields, idx = params
    e = base_entity(name, pos, rot, folder_id=folder)
    e[PROCEDURAL] = {**fields, **RENDER_DEFAULTS, "type": idx, "material": material}
    entities.append(e)
    return e


def box(entities, name, corner, size, material, rot=IDENT, folder=0, motion=0, friction=0.5):
    """Corner-pivot box spanning corner..corner+size in the entity's local frame."""
    return shape(entities, name, corner, box_params(*size), material, rot, folder, motion, friction)


def vbox(entities, name, corner, size, material, rot=IDENT, folder=0):
    return visual(entities, name, corner, box_params(*size), material, rot, folder)


def wall_run(entities, name, start_xz, end_xz, height, thickness, material,
             openings=(), base_y=GRADE, folder=0):
    """One welded slab with up to 8 rectangular holes, running start->end on the XZ plane.

    Thickness extends to the LEFT of travel -- flip start/end to put the slab on the other
    side of its baseline. Openings are (u, y, w, h) with u measured along the run from
    `start` and y measured up from `base_y`."""
    (sx, sz), (ex, ez) = start_xz, end_xz
    dx, dz = ex - sx, ez - sz
    length = math.hypot(dx, dz)
    for u, y, w, h in openings:
        assert 0.0 <= u and u + w <= length, f"{name}: opening u={u} w={w} outside run of {length}"
        assert 0.0 <= y and y + h <= height, f"{name}: opening y={y} h={h} outside height {height}"
    return shape(entities, name, (sx, base_y, sz),
                 wall_params(length, height, thickness, openings), material,
                 run_quat(dx, dz), folder)


def platform(entities, name, p_start, p_end, width, material, thickness=0.4, friction=0.6, folder=0):
    """Sloped or level slab whose top-face centreline runs p_start -> p_end."""
    e = wa.add_platform(entities, name, p_start, p_end, width, thickness=thickness, friction=friction)
    e[PROCEDURAL]["material"] = material
    e[FOLDER]["folderId"] = folder
    return e


def folder_entity(folders, name, parent=0):
    fid = next_id()
    folders.append({SCENE_FOLDER: {"folderId": fid, "name": name, "parentFolder": parent}})
    return fid


def light(entities, name, pos, rot=IDENT, folder=0):
    e = base_entity(name, pos, rot, folder_id=folder)
    entities.append(e)
    return e


def probe_box(entities, name, lo, hi, capture=(0.0, 0.0, 0.0), resolution=PROBE_RES_128,
              fade=0.0, folder=0):
    """Axis-aligned box probe over world extents lo..hi. transform.scale is HALF-EXTENTS.

    Per docs/renderer/reflection_probes.md:
      * One lighting environment per probe; never span an occluding wall.
      * Overshoot the room bounds by less than the wall thickness, so the fade band ends
        up buried in the wall rather than dimming the interior surface.
      * `fade` is metres inward from every face, so only use it where the overshoot is at
        least as large -- or on a volume that is genuinely open on that side and should
        blend into the enclosing probe.
      * `capture` is world units from the centre. Bias it toward the brightest part of the
        volume: too bright is recoverable, too dark is not. Keep it out of every mesh.
      * Overlaps resolve smallest-volume-first, so nesting a tight probe inside a loose one
        is the intended way to special-case an interior.
    probeId comes from name_id(name): it names assets/probes/probe_<id>.wprobe, so it has to
    survive re-runs and not collide with another generator's ids."""
    center = tuple((a + b) * 0.5 for a, b in zip(lo, hi))
    half = tuple((b - a) * 0.5 for a, b in zip(lo, hi))
    e = base_entity(name, center, IDENT, half, folder_id=folder)
    add_reflection_probe(e, name_id(name), capture_offset=capture, resolution=resolution,
                         fade_margin=fade)
    entities.append(e)
    return e


# =============================================================================
# site: ground + perimeter
# =============================================================================
# Compound interior x in [-60, 60], z in [-50, 50]. Ground is one solid slab whose top
# is exactly GRADE; nothing in this level digs below it, so no excavation is needed and
# no interior can fill with terrain.
GX0, GX1, GZ0, GZ1 = -60.0, 60.0, -50.0, 50.0
PERIM_H, PERIM_T = 4.5, 0.6
GATE_W, GATE_H = 7.0, 3.6
GATE_X0, GATE_X1 = -3.5, 3.5      # gate opening in world X

F_SITE = folder_entity(folders, "Site")

box(entities, "Ground", (GX0 - 14.0, GRADE - 2.0, GZ0 - 14.0),
    ((GX1 - GX0) + 28.0, 2.0, (GZ1 - GZ0) + 28.0), M["sand"], folder=F_SITE, friction=0.8)

# Four wall_params slabs, each ONE entity carrying its own openings. Runs are ordered so
# thickness always falls inboard; the E/W runs stop short of the N/S runs, so the only
# faces that ever touch are back-to-back at the corners.
# Untinted concrete: the 0.5 tint went near-black on every shaded run and the perimeter
# read as two different walls depending on sun facing.
wall_run(entities, "Perimeter South", (GX0, GZ0), (GX1, GZ0), PERIM_H, PERIM_T, M["concrete"],
         openings=[(GATE_X0 - GX0, 0.0, GATE_W, GATE_H),
                   (18.0, 0.0, 0.9, 2.2),         # personnel door
                   (34.0, 2.6, 1.4, 0.5),         # observation slits: thin and high-contrast
                   (86.0, 2.6, 1.4, 0.5)],
         folder=F_SITE)
wall_run(entities, "Perimeter North", (GX1, GZ1), (GX0, GZ1), PERIM_H, PERIM_T, M["concrete"],
         openings=[(52.0, 0.0, 6.0, 3.6)], folder=F_SITE)
wall_run(entities, "Perimeter East", (GX1, GZ0 + PERIM_T), (GX1, GZ1 - PERIM_T), PERIM_H, PERIM_T,
         M["concrete"], openings=[(30.0, 2.6, 1.4, 0.5), (62.0, 2.6, 1.4, 0.5)], folder=F_SITE)
wall_run(entities, "Perimeter West", (GX0, GZ1 - PERIM_T), (GX0, GZ0 + PERIM_T), PERIM_H, PERIM_T,
         M["concrete"], openings=[(46.0, 2.6, 1.4, 0.5)], folder=F_SITE)

# Buttresses: silhouette break-up, and each throws its own long sun shadow. They embed
# 0.2m into the wall rather than butting against it -- overlapping volumes are fine,
# coincident faces are what z-fight. Any that would stand in a gate opening is skipped.
NORTH_GATE_X0, NORTH_GATE_X1 = GX1 - 58.0, GX1 - 52.0     # north run is measured from +X
for i in range(13):
    bx = GX0 + 2.0 + i * 9.5
    if bx + 1.2 < GATE_X0 - 0.5 or bx > GATE_X1 + 0.5:
        box(entities, f"Buttress S {i}", (bx, GRADE, GZ0 + PERIM_T - 0.2), (1.2, 3.2, 1.1),
            M["concrete"], folder=F_SITE)
    if bx + 1.2 < NORTH_GATE_X0 - 0.5 or bx > NORTH_GATE_X1 + 0.5:
        box(entities, f"Buttress N {i}", (bx, GRADE, GZ1 - PERIM_T - 0.9), (1.2, 3.2, 1.1),
            M["concrete"], folder=F_SITE)

# Gate furniture, all inboard of the wall so nothing intersects the jambs.
box(entities, "Gate Post L", (GATE_X0 - 1.1, GRADE, GZ0 + PERIM_T + 0.05), (0.9, 5.2, 1.0),
    M["concrete"], folder=F_SITE)
box(entities, "Gate Post R", (GATE_X1 + 0.2, GRADE, GZ0 + PERIM_T + 0.05), (0.9, 5.2, 1.0),
    M["concrete"], folder=F_SITE)
box(entities, "Gate Boom", (GATE_X0 + 0.1, 3.1, GZ0 + PERIM_T + 0.2), (6.8, 0.35, 0.35),
    M["hazard"], folder=F_SITE)

# Guard house just inside the gate, door and window facing the road. Shell follows the
# room_boxes tiling rule; the front is a wall_run so the openings punch one welded slab.
GH_X0, GH_Z0, GH_W, GH_D, GH_H = 4.2, -47.0, 3.4, 3.0, 2.6
box(entities, "Gatehouse Floor", (GH_X0, GRADE, GH_Z0), (GH_W, 0.15, GH_D), M["floor"], folder=F_SITE)
box(entities, "Gatehouse Wall East", (GH_X0 + GH_W - 0.15, GRADE + 0.15, GH_Z0), (0.15, GH_H, GH_D),
    M["panel"], folder=F_SITE)
box(entities, "Gatehouse Wall South", (GH_X0 + 0.15, GRADE + 0.15, GH_Z0), (GH_W - 0.3, GH_H, 0.15),
    M["panel"], folder=F_SITE)
box(entities, "Gatehouse Wall North", (GH_X0 + 0.15, GRADE + 0.15, GH_Z0 + GH_D - 0.15), (GH_W - 0.3, GH_H, 0.15),
    M["panel"], folder=F_SITE)
wall_run(entities, "Gatehouse Wall West", (GH_X0, GH_Z0 + GH_D - 0.15), (GH_X0, GH_Z0), GH_H, 0.15,
         M["panel"], openings=[(0.35, 0.0, 0.9, 2.1), (1.6, 1.0, 1.0, 0.9)],
         base_y=GRADE + 0.15, folder=F_SITE)
box(entities, "Gatehouse Roof", (GH_X0 - 0.3, GRADE + 0.15 + GH_H, GH_Z0 - 0.3),
    (GH_W + 0.6, 0.18, GH_D + 0.6), M["galv"], folder=F_SITE)
e = light(entities, "Gatehouse Light", (GH_X0 + GH_W * 0.5, GRADE + GH_H - 0.15, GH_Z0 + GH_D * 0.5),
          folder=F_SITE)
add_sphere_light(e, color=(1.0, 0.93, 0.80), intensity=14.0, radius=0.09, draw_range=9.0,
                 draw_emissive=True)

# Road network: thin asphalt slabs sunk 0.02 into the ground slab so the top sits 0.06
# proud and no coincident co-facing planes exist. Spine runs gate -> yard centre, one
# branch west to the motor pool, one east toward the helipad.
ROAD_Y, ROAD_H = GRADE - 0.02, 0.08
# Runs out past the gate onto the approach: stopping dead at the wall read as a slab edge.
box(entities, "Road Spine", (-3.0, ROAD_Y, GZ0 - 14.0), (6.0, ROAD_H, 84.0), M["asphalt"], folder=F_SITE, friction=0.9)
box(entities, "Road West Branch", (-22.0, ROAD_Y, -26.0), (19.0, ROAD_H, 6.0), M["asphalt"], folder=F_SITE, friction=0.9)
box(entities, "Road East Branch", (3.0, ROAD_Y, 16.5), (27.0, ROAD_H, 6.0), M["asphalt"], folder=F_SITE, friction=0.9)
for i in range(14):
    vbox(entities, f"Road Dash {i}", (-0.08, GRADE + 0.061, GZ0 - 11.0 + i * 6.0), (0.16, 0.015, 2.6),
         M["marking"], folder=F_SITE)
# Concrete apron in front of the hangar door.
box(entities, "Hangar Apron", (-23.4, ROAD_Y, -10.0), (13.4, ROAD_H, 16.0), M["concrete"], folder=F_SITE, friction=0.9)


# =============================================================================
# sun
# =============================================================================
F_LIGHT = folder_entity(folders, "Lighting")

# Low sun out of the south-east: long shadows across the yard, and it rakes straight
# through the hangar's east-facing door and the bunker's south embrasures. The vector is
# the direction light TRAVELS, so -X/+Z means "coming from the south-east".
SUN_DIR = (-0.52, -0.38, 0.62)
sun = light(entities, "Sun", (0.0, 30.0, 0.0), face_dir(*SUN_DIR), folder=F_LIGHT)
add_directional_light(sun, color=(1.0, 0.93, 0.82), intensity=6.0, priority=10, angular_radius_deg=0.6)
add_skybox(sun, ENV_MAP, intensity=1.0)


# =============================================================================
# hangar -- colour bleed and a bounce-only back third
# =============================================================================
F_HANGAR = folder_entity(folders, "Hangar")

HT = 0.5                                   # shell thickness
HX0, HX1 = -52.0, -24.0                    # interior x
HZ0, HZ1 = -14.0, 10.0                     # interior z
HXO0, HXO1 = HX0 - HT, HX1 + HT            # outer x
HZO0, HZO1 = HZ0 - HT, HZ1 + HT            # outer z
HFY = GRADE + 0.2                          # interior floor level
HH = 10.0                                  # interior ceiling level
DOOR_W, DOOR_H = 14.0, 7.0

box(entities, "Hangar Floor", (HXO0, GRADE, HZO0), (HXO1 - HXO0, 0.2, HZO1 - HZO0),
    M["floor"], folder=F_HANGAR, friction=0.7)
box(entities, "Hangar Roof", (HXO0, HH, HZO0), (HXO1 - HXO0, HT, HZO1 - HZO0),
    M["steel_rough"], folder=F_HANGAR)
# West (back) and east (door) walls take the full Z run; south and north fill the gap.
box(entities, "Hangar Back Wall", (HXO0, HFY, HZO0), (HT, HH - HFY, HZO1 - HZO0),
    M["red"], folder=F_HANGAR)
wall_run(entities, "Hangar Door Wall", (HX1, HZO1), (HX1, HZO0), HH - HFY, HT, M["steel_rough"],
         openings=[((HZO1 - HZO0) * 0.5 - DOOR_W * 0.5, 0.0, DOOR_W, DOOR_H)],
         base_y=HFY, folder=F_HANGAR)
wall_run(entities, "Hangar Wall South", (HX1, HZ0), (HX0, HZ0), HH - HFY, HT, M["concrete"],
         openings=[(3.0 + i * 6.0, 7.4, 3.2, 1.1) for i in range(4)], base_y=HFY, folder=F_HANGAR)
wall_run(entities, "Hangar Wall North", (HX0, HZ1), (HX1, HZ1), HH - HFY, HT, M["concrete"],
         openings=[(3.0 + i * 6.0, 7.4, 3.2, 1.1) for i in range(4)], base_y=HFY, folder=F_HANGAR)

# Roof trusses: mid-frequency occluders right under the roof, so the clerestory shafts
# arrive at the red wall as stripes rather than a wash.
for i in range(7):
    tz = HZ0 + 1.5 + i * 3.5
    box(entities, f"Hangar Truss {i}", (HX0, HH - 0.9, tz - 0.15), (HX1 - HX0, 0.25, 0.3),
        M["steel"], folder=F_HANGAR)
    box(entities, f"Hangar Truss Web {i}", (HX0, HH - 1.5, tz - 0.08), (HX1 - HX0, 0.12, 0.16),
        M["steel"], folder=F_HANGAR)

# Work lights over the FRONT half only -- the back third is deliberately left to bounce.
for i in range(4):
    lx = HX1 - 3.5 - i * 5.0
    cz = (HZ0 + HZ1) * 0.5
    e = light(entities, f"Hangar Work Light {i}", (lx, HH - 1.9, cz), face_dir(0.0, -1.0, 0.0),
              folder=F_HANGAR)
    add_area_light(e, color=(1.0, 0.92, 0.80), intensity=190.0, half_width=2.2, half_height=1.1,
                   draw_range=26.0, draw_emissive=True)
    vbox(entities, f"Hangar Work Light Housing {i}", (lx - 2.4, HH - 1.75, cz - 1.3),
         (4.8, 0.25, 2.6), M["steel_rough"], folder=F_HANGAR)

# Contents: a helicopter mock-up, a gantry and crates -- all of it occludes both the work
# lights and the door shaft. The old fixed-wing read as floor-height slabs and a spike fin.
HELI_X, HELI_Z = HX0 + 14.0, -2.0
HELI_FY = HFY + 0.62                       # fuselage underside, riding on the skids
for side, sz in (("L", -1.1), ("R", 0.9)):
    # roll(90) swings the cylinder's local +Y onto world -X: skid tubes run fore-aft.
    shape(entities, f"Heli Skid {side}", (HELI_X, HFY + 0.15, HELI_Z + sz + 0.1),
          cylinder_params(0.10, 4.2, 16), M["galv"], rot=roll(90.0), folder=F_HANGAR)
    for st in (0, 1):
        box(entities, f"Heli Skid Strut {side}{st}", (HELI_X - 1.4 + st * 2.6, HFY + 0.2, HELI_Z + sz),
            (0.13, 0.5, 0.13), M["galv"], folder=F_HANGAR)
box(entities, "Heli Fuselage", (HELI_X - 2.3, HELI_FY, HELI_Z - 1.1), (4.6, 1.9, 2.2),
    M["olive"], folder=F_HANGAR)
box(entities, "Heli Nose", (HELI_X - 3.5, HELI_FY, HELI_Z - 0.9), (1.2, 1.3, 1.8),
    M["olive"], folder=F_HANGAR)
# Windshield leans back from the nose top toward the cabin roof.
visual(entities, "Heli Windshield", (HELI_X - 3.5, HELI_FY + 1.3, HELI_Z - 0.9),
       box_params(0.08, 1.2, 1.8), M["navy"], rot=roll(-50.0), folder=F_HANGAR)
box(entities, "Heli Engine", (HELI_X - 1.4, HELI_FY + 1.9, HELI_Z - 0.7), (2.6, 0.55, 1.4),
    M["od_light"], folder=F_HANGAR)
shape(entities, "Heli Tail Boom", (HELI_X + 4.4, HELI_FY + 1.45, HELI_Z),
      cylinder_params(0.30, 4.2, 24), M["olive"], rot=roll(90.0), folder=F_HANGAR)
box(entities, "Heli Tail Fin", (HELI_X + 6.1, HELI_FY + 1.5, HELI_Z - 0.07), (0.7, 1.6, 0.14),
    M["olive"], folder=F_HANGAR)
box(entities, "Heli Stabilizer", (HELI_X + 5.9, HELI_FY + 1.35, HELI_Z - 0.7), (0.7, 0.10, 1.4),
    M["olive"], folder=F_HANGAR)
# Tail rotor as two crossed thin blades -- a solid disc read as a giant black dish.
box(entities, "Heli Tail Blade A", (HELI_X + 6.38, HELI_FY + 1.75, HELI_Z + 0.14), (0.14, 1.5, 0.05),
    M["rubber"], folder=F_HANGAR)
box(entities, "Heli Tail Blade B", (HELI_X + 5.7, HELI_FY + 2.43, HELI_Z + 0.20), (1.5, 0.14, 0.05),
    M["rubber"], folder=F_HANGAR)
shape(entities, "Heli Tail Hub", (HELI_X + 6.45, HELI_FY + 2.5, HELI_Z + 0.18),
      cylinder_params(0.12, 0.14, 14), M["steel"], rot=pitch(90.0), folder=F_HANGAR)
shape(entities, "Heli Rotor Hub", (HELI_X, HELI_FY + 2.45, HELI_Z),
      cylinder_params(0.22, 0.5, 20), M["steel"], folder=F_HANGAR)
# Two crossed blade slabs at slightly different heights so they overlap without touching.
box(entities, "Heli Blade A", (HELI_X - 5.4, HELI_FY + 2.72, HELI_Z - 0.20), (10.8, 0.06, 0.40),
    M["rubber"], folder=F_HANGAR)
box(entities, "Heli Blade B", (HELI_X - 0.20, HELI_FY + 2.80, HELI_Z - 5.4), (0.40, 0.06, 10.8),
    M["rubber"], folder=F_HANGAR)

# Rail is inset inside the legs on both x and z: flush faces would be coplanar and
# co-facing with the legs' own, which z-fights.
box(entities, "Hangar Gantry Leg A", (HX0 + 2.0, HFY, 5.4), (0.4, 6.2, 0.4), M["galv"], folder=F_HANGAR)
box(entities, "Hangar Gantry Leg B", (HX0 + 25.6, HFY, 5.4), (0.4, 6.2, 0.4), M["galv"], folder=F_HANGAR)
box(entities, "Hangar Gantry Rail", (HX0 + 2.05, 6.2, 5.45), (23.9, 0.4, 0.3), M["galv"], folder=F_HANGAR)

# Wall-side workshop clutter: bench, tool lockers, gas bottles, drums on a pallet.
box(entities, "Hangar Bench", (HX0 + 0.7, HFY, -12.6), (5.2, 0.95, 1.1), M["galv"], folder=F_HANGAR)
for i in range(3):
    box(entities, f"Hangar Locker {i}", (HX0 + 6.6 + i * 0.85, HFY, -13.2), (0.75, 1.9, 0.7),
        M["navy"], folder=F_HANGAR)
for i in range(4):
    shape(entities, f"Hangar Gas Bottle {i}", (HX0 + 10.4 + i * 0.5, HFY + 0.75, -13.1),
          capsule_params(0.18, 1.1, 20, 10), M["hazard" if i % 2 else "steel"], folder=F_HANGAR)
vbox(entities, "Hangar Pallet", (HX0 + 13.0, HFY, -13.4), (1.6, 0.14, 1.3), M["od_light"], folder=F_HANGAR)
for i in range(3):
    shape(entities, f"Hangar Drum {i}", (HX0 + 13.4 + (i % 2) * 0.8, HFY + 0.14 + 0.45, -13.1 + (i // 2) * 0.7),
          cylinder_params(0.4, 0.9, 26), M["container_red" if i == 1 else "olive"], folder=F_HANGAR)

for i in range(9):
    # Back row ends at z=9.7, clear of the north wall's inner face at z=10; the near
    # column starts at x=-49.4, clear of gantry leg A.
    box(entities, f"Hangar Crate {i}", (HX0 + 2.6 + (i % 3) * 1.9, HFY, 5.4 + (i // 3) * 1.5),
        (1.7, 1.1, 1.3), M["olive"] if i % 2 else M["od_light"], folder=F_HANGAR)

# Bounds sit mid-shell -- a 0.25 overshoot into a 0.5 wall, so every interior face is inside
# the volume and every exterior face is outside it. fadeMargin stays 0: the shell is sealed
# and never blends into a neighbour, and any inward fade would dim the very walls sitting on
# the boundary. Capture is pushed toward the door, the brightest part of the interior, and
# lands in open air clear of the fuselage, gantry and crates.
probe_box(entities, "Probe Hangar", (HX0 - HT * 0.5, GRADE, HZ0 - HT * 0.5),
          (HX1 + HT * 0.5, HH + HT * 0.5, HZ1 + HT * 0.5),
          capture=(8.0, -0.5, 5.0), resolution=PROBE_RES_256, folder=F_HANGAR)


# =============================================================================
# bunker -- sealed, bounce-only, behind a blast baffle
# =============================================================================
F_BUNKER = folder_entity(folders, "Bunker")

BT = 0.6
BX0, BX1 = 16.0, 34.0            # interior x
BZ0, BZ1 = -18.0, -6.0           # interior z
BXO0, BXO1 = BX0 - BT, BX1 + BT
BZO0, BZO1 = BZ0 - BT, BZ1 + BT
BFY = GRADE + 0.2                # interior floor level
BH = 3.4                         # interior height
BCY = BFY + BH                   # underside of the roof slab
DOOR_U, BDOOR_W, BDOOR_H = 8.5, 2.0, 2.4

box(entities, "Bunker Floor", (BXO0, GRADE, BZO0), (BXO1 - BXO0, 0.2, BZO1 - BZO0),
    M["floor"], folder=F_BUNKER, friction=0.7)
box(entities, "Bunker Roof", (BXO0, BCY, BZO0), (BXO1 - BXO0, 1.4, BZO1 - BZO0),
    M["concrete_dark"], folder=F_BUNKER)
# Side walls carry the colour: white one side, blue the other, so the green console and
# the red strip each read against a different backing.
box(entities, "Bunker Wall West", (BXO0, BFY, BZO0), (BT, BH, BZO1 - BZO0), M["white"], folder=F_BUNKER)
box(entities, "Bunker Wall East", (BX1, BFY, BZO0), (BT, BH, BZO1 - BZO0), M["blue"], folder=F_BUNKER)
# South wall: two embrasure slits, the only direct exterior light path.
wall_run(entities, "Bunker Wall South", (BX1, BZ0), (BX0, BZ0), BH, BT, M["concrete"],
         openings=[(4.0, 2.1, 2.6, 0.45), (11.4, 2.1, 2.6, 0.45)], base_y=BFY, folder=F_BUNKER)
# North wall: the doorway.
wall_run(entities, "Bunker Wall North", (BX0, BZ1), (BX1, BZ1), BH, BT, M["concrete"],
         openings=[(DOOR_U, 0.0, BDOOR_W, BDOOR_H)], base_y=BFY, folder=F_BUNKER)

# Blast baffle: an L standing off the doorway so there is no straight line from open sky
# to the door. Everything that gets in through it has already bounced at least once.
DOOR_CX = BX0 + DOOR_U + BDOOR_W * 0.5
box(entities, "Bunker Baffle Face", (DOOR_CX - 3.4, GRADE, BZO1 + 2.2), (5.6, 3.2, 0.5),
    M["concrete_dark"], folder=F_BUNKER)
box(entities, "Bunker Baffle Return", (DOOR_CX - 3.4, GRADE, BZO1), (0.5, 3.2, 2.2),
    M["concrete_dark"], folder=F_BUNKER)
box(entities, "Bunker Baffle Lid", (DOOR_CX - 3.4, 3.2, BZO1), (5.6, 0.4, 2.7),
    M["concrete_dark"], folder=F_BUNKER)

# Earth berm banked against three sides, so it reads as dug in without any excavation.
for name, corner, size in (
        ("Bunker Berm West", (BXO0 - 3.0, GRADE, BZO0 - 3.0), (3.0, 2.2, (BZO1 - BZO0) + 6.0)),
        ("Bunker Berm East", (BXO1, GRADE, BZO0 - 3.0), (3.0, 2.2, (BZO1 - BZO0) + 6.0)),
        ("Bunker Berm South", (BXO0, GRADE, BZO0 - 3.0), (BXO1 - BXO0, 1.6, 3.0))):
    box(entities, name, corner, size, M["sand"], folder=F_BUNKER)

# Interior: console bank (green), warning strip (red), map table, lockers, pillars.
box(entities, "Bunker Console Bank", (BX0 + 1.0, BFY, BZ1 - 1.4), (7.0, 1.05, 1.0),
    M["concrete_dark"], folder=F_BUNKER)
# The bunker is the level's EMISSIVE-MESH case: the screens and the warning strip are real
# geometry with an emissive material, gathered as triangle lights, with no analytic light
# paired to them. That keeps attribution clean (nothing here is lit twice) and it is the one
# interior whose emitters are actual surfaces a probe bake can see -- analytic light proxies
# elsewhere are auto-hidden during a bake.
for i in range(4):
    visual(entities, f"Bunker Screen {i}", (BX0 + 1.3 + i * 1.7, BFY + 1.05, BZ1 - 1.35),
           box_params(1.2, 0.75, 0.06), M["em_green"], rot=pitch(-14.0), folder=F_BUNKER)

visual(entities, "Bunker Warning Strip", (BX1 - 0.72, BFY + 2.7, BZ0 + 1.5),
       box_params(0.08, 0.16, 9.0), M["em_red"], folder=F_BUNKER)

box(entities, "Bunker Map Table", (BX0 + 9.5, BFY, BZ0 + 4.6), (3.4, 0.9, 2.0), M["od_light"], folder=F_BUNKER)
for i in range(4):
    box(entities, f"Bunker Locker {i}", (BX1 - 1.5, BFY, BZ0 + 1.0 + i * 1.1), (0.7, 2.0, 0.95),
        M["olive"], folder=F_BUNKER)
for i in range(3):
    shape(entities, f"Bunker Pillar {i}", (BX0 + 4.5 + i * 4.5, BFY + BH * 0.5, (BZ0 + BZ1) * 0.5),
          cylinder_params(0.45, BH, 26), M["concrete"], folder=F_BUNKER)

# Mid-shell again (0.3 overshoot into a 0.6 wall). Capture biased south toward the embrasure
# slits -- the brightest thing in the room -- while clearing the pillars, map table and
# console bank. Biasing dark here would be the destructive direction.
probe_box(entities, "Probe Bunker", (BX0 - BT * 0.5, BFY, BZ0 - BT * 0.5),
          (BX1 + BT * 0.5, BCY + 0.2, BZ1 + BT * 0.5),
          capture=(-1.0, 0.5, -4.0), resolution=PROBE_RES_128, folder=F_BUNKER)


# =============================================================================
# culvert -- one-dimensional GI falloff
# =============================================================================
F_CULVERT = folder_entity(folders, "Culvert")

# Surface-laid, not buried: Pipe is centred on its local Y axis, and pitch(90) swings that
# axis onto world +Z. Centre height = outer radius, so the pipe rests on grade and its bore
# floor sits one wall-thickness up -- a short ramp at each mouth makes it enterable.
CULV_X, CULV_RO, CULV_RI, CULV_LEN = 8.0, 2.3, 2.0, 11.0
CULV_Y = GRADE + CULV_RO - 0.15            # seated 0.15 into grade so it doesn't perch on a tangent line
CULV_Z0 = -2.0
for i in range(3):
    cz = CULV_Z0 + CULV_LEN * (i + 0.5)
    shape(entities, f"Culvert Pipe {i}", (CULV_X, CULV_Y, cz),
          pipe_params(CULV_RO, CULV_RI, CULV_LEN, 48), M["concrete_pipe"], rot=pitch(90.0), folder=F_CULVERT)
# Ring collars at the two mouths. Ring is a thin annular slab in local XZ with local +Y as
# its normal; pitch(90) stands it up facing +Z, so it frames the bore instead of blocking it.
for i, cz in enumerate((CULV_Z0 + 0.12, CULV_Z0 + 3 * CULV_LEN - 0.12)):
    shape(entities, f"Culvert Collar {i}", (CULV_X, CULV_Y, cz),
          ring_params(CULV_RO + 0.35, CULV_RI, 48), M["concrete_pipe"], rot=pitch(90.0), folder=F_CULVERT)
platform(entities, "Culvert Ramp South", (CULV_X, GRADE, CULV_Z0 - 2.4), (CULV_X, GRADE + 0.18, CULV_Z0),
         3.2, M["concrete"], thickness=0.3, friction=0.75, folder=F_CULVERT)
platform(entities, "Culvert Ramp North", (CULV_X, GRADE, CULV_Z0 + 3 * CULV_LEN + 2.4),
         (CULV_X, GRADE + 0.18, CULV_Z0 + 3 * CULV_LEN), 3.2, M["concrete"], thickness=0.3,
         friction=0.75, folder=F_CULVERT)
# One weak lamp a third of the way in, so bounce-from-the-mouths is separable from
# direct falloff.
LAMP_Z = CULV_Z0 + CULV_LEN * 1.1
e = light(entities, "Culvert Lamp", (CULV_X, CULV_Y + 1.4, LAMP_Z), face_dir(0.0, -1.0, 0.0),
          folder=F_CULVERT)
# Weak relative to the yard, but 9 crushed the whole bore to black, which shows the falloff
# gradient nothing: the point is to read banding along it, not to prove it goes dark.
add_sphere_light(e, color=(0.95, 0.92, 0.85), intensity=38.0, radius=0.1, draw_range=18.0,
                 draw_emissive=True)

# One probe PER SEGMENT rather than one for the whole run: the whole point of the culvert is
# that its lighting changes along its length, and a single probe over a 33m gradient is
# exactly the "one lighting environment per probe" rule being broken. Bounds hug the bore and
# overshoot 0.15 into the 0.3 pipe wall. fadeMargin stays 0 -- an inward fade would dim the
# bore wall, and the cost is a hard seam at each joint, inside a rough concrete tube.
# Each capture leans toward its own segment's bright end -- the open mouth for the two end
# segments, the lamp for the middle one -- without landing on the lamp proxy itself.
for i, cap_z in enumerate((-2.5, -2.0, 2.5)):
    z0 = CULV_Z0 + i * CULV_LEN
    probe_box(entities, f"Probe Culvert {i}",
              (CULV_X - CULV_RI - 0.15, CULV_Y - CULV_RI - 0.15, z0),
              (CULV_X + CULV_RI + 0.15, CULV_Y + CULV_RI + 0.15, z0 + CULV_LEN),
              capture=(0.0, 0.0, cap_z), folder=F_CULVERT)


# =============================================================================
# helipad -- many weak emitters
# =============================================================================
F_PAD = folder_entity(folders, "Helipad")

PAD_X, PAD_Z, PAD_R = 40.0, 28.0, 12.0
PAD_TOP = GRADE + 0.32
shape(entities, "Helipad Deck", (PAD_X, GRADE + 0.16, PAD_Z), cylinder_params(PAD_R, 0.32, 80),
      M["asphalt_pad"], folder=F_PAD, friction=0.9)
visual(entities, "Helipad Ring", (PAD_X, PAD_TOP + 0.01, PAD_Z),
       ring_params(PAD_R - 0.6, PAD_R - 1.3, 80), M["marking"], folder=F_PAD)
vbox(entities, "Helipad H Left", (PAD_X - 2.6, PAD_TOP + 0.01, PAD_Z - 3.2), (0.7, 0.02, 6.4),
     M["marking"], folder=F_PAD)
vbox(entities, "Helipad H Right", (PAD_X + 1.9, PAD_TOP + 0.01, PAD_Z - 3.2), (0.7, 0.02, 6.4),
     M["marking"], folder=F_PAD)
# Crossbar spans only the gap between the uprights -- overlapping them would put two
# coplanar top faces at the same height.
vbox(entities, "Helipad H Bar", (PAD_X - 1.9, PAD_TOP + 0.01, PAD_Z - 0.45), (3.8, 0.02, 0.9),
     M["marking"], folder=F_PAD)

for i in range(12):
    a = i / 12.0 * math.tau
    lx, lz = PAD_X + math.cos(a) * (PAD_R + 0.9), PAD_Z + math.sin(a) * (PAD_R + 0.9)
    box(entities, f"Helipad Light Post {i}", (lx - 0.09, GRADE, lz - 0.09), (0.18, 0.42, 0.18),
        M["galv"], folder=F_PAD)
    # Sphere bottom overlaps the post top so the fixture reads as mounted, not floating.
    e = light(entities, f"Helipad Light {i}", (lx, GRADE + 0.5, lz), folder=F_PAD)
    add_sphere_light(e, color=(0.62, 0.84, 1.0), intensity=12.0, radius=0.13, draw_range=11.0,
                     draw_emissive=True)

box(entities, "Helipad Windsock Mast", (PAD_X - PAD_R - 3.0, GRADE, PAD_Z + 6.0), (0.22, 6.0, 0.22),
    M["galv"], folder=F_PAD)
# roll(-90) swings the cone's local +Y onto world +X, so it streams away from the mast.
shape(entities, "Helipad Windsock", (PAD_X - PAD_R - 2.8, GRADE + 5.6, PAD_Z + 6.11),
      cone_params(0.55, 2.2, 22), M["hazard"], rot=roll(-90.0), folder=F_PAD)
# West of the pad centreline, so the tank's 6.2m body stays clear of the NE tower's apron.
BOWSER_X, BOWSER_Z = PAD_X - 2.0, PAD_Z + PAD_R + 4.0
box(entities, "Helipad Bowser Pad", (BOWSER_X - 4.6, GRADE - 0.02, BOWSER_Z - 2.4), (9.2, 0.1, 4.8),
    M["concrete"], folder=F_PAD, friction=0.9)
shape(entities, "Helipad Fuel Bowser", (BOWSER_X, GRADE + 1.5, BOWSER_Z),
      capsule_params(1.5, 6.2, 32, 16), M["steel"], rot=roll(90.0), folder=F_PAD)
for i in (0, 1):
    box(entities, f"Helipad Bowser Cradle {i}", (BOWSER_X - 2.4 + i * 4.0, GRADE + 0.08, BOWSER_Z - 1.1),
        (0.8, 0.9, 2.2), M["concrete_dark"], folder=F_PAD)

# Open-air apron, so there is no wall to bury a fade band in -- instead it fades over its
# outer 1.5m straight into the yard probe that encloses it. The twelve pad emitters still
# light the bake even though their proxies are hidden from it, so this genuinely differs
# from the yard environment rather than duplicating it.
probe_box(entities, "Probe Helipad", (PAD_X - PAD_R - 1.0, GRADE, PAD_Z - PAD_R - 1.0),
          (PAD_X + PAD_R + 1.0, 7.0, PAD_Z + PAD_R + 1.0), capture=(0.0, 2.5, 0.0),
          fade=1.5, folder=F_PAD)


# =============================================================================
# ammo revetment -- tight concave occlusion
# =============================================================================
F_REV = folder_entity(folders, "Revetment")

REV_X, REV_Z, HESCO = -34.0, 30.0, 2.2


def hesco_run(name, start, end, courses=2):
    """Stacked 2.2m HESCO cells along a straight run. One shared fill material -- per-cube
    shade alternation read as a checkerboard once real textures landed -- but a bare run of
    butted cubes then read as one smooth panel, so each cell gets a dark gabion frame: a
    post at its leading edge and a band at its top. That is what makes the cells legible."""
    (sx, sz), (ex, ez) = start, end
    dx, dz = ex - sx, ez - sz
    length = math.hypot(dx, dz)
    n = max(1, int(round(length / HESCO)))
    # Every run in this level is axis-aligned, so the frames stay unrotated: they are thin
    # along the run and span the cell's full thickness, standing 0.04 proud on both faces.
    along_z = abs(dz) > abs(dx)
    span = HESCO + 0.08
    for c in range(courses):
        y = GRADE + c * HESCO
        for i in range(n):
            t = i / n
            cx0, cz0 = sx + dx * t, sz + dz * t
            box(entities, f"{name} {c}-{i}", (cx0, y, cz0), (HESCO, HESCO, HESCO),
                M["hesco"], folder=F_REV)
            post = (span, HESCO, 0.09) if along_z else (0.09, HESCO, span)
            vbox(entities, f"{name} Post {c}-{i}", (cx0 - 0.04, y, cz0 - 0.04), post,
                 M["concrete_dark"], folder=F_REV)
        # One band per course at the cell tops, spanning the whole run.
        band = (span, 0.11, length) if along_z else (length, 0.11, span)
        vbox(entities, f"{name} Band {c}", (min(sx, ex) - 0.04, y + HESCO - 0.11, min(sz, ez) - 0.04),
             band, M["concrete_dark"], folder=F_REV)


# Pad oversails the HESCO footprint by 0.6 all round; flush edges would be coplanar
# and co-facing with the barrier faces sitting on them.
box(entities, "Revetment Pad", (REV_X - 9.4, GRADE, REV_Z - 7.2), (18.8, 0.12, 16.6),
    M["concrete"], folder=F_REV)
hesco_run("Revetment West", (REV_X - 8.8, REV_Z - 6.6), (REV_X - 8.8, REV_Z + 6.6))
hesco_run("Revetment East", (REV_X + 6.6, REV_Z - 6.6), (REV_X + 6.6, REV_Z + 6.6))
hesco_run("Revetment Back", (REV_X - 6.6, REV_Z + 6.6), (REV_X + 6.6, REV_Z + 6.6))

# Interior clear span is x in [-40.6, -27.4], z in [23.4, 36.6]; the stack, the drums and
# the lamp mast each keep to their own slice of it so nothing grows through a barrier.
REV_TOP = GRADE + 0.12
# Per-column stack heights rather than a flat 2-course block: a level top edge read as one
# extruded slab from the shot angle, and the varied skyline is what sells hand-packed.
CRATE_H = ((2, 1), (3, 2), (1, 2), (2, 1), (3, 2))
CRATE_MATS = (M["olive"], M["od_light"], M["container_red"], M["olive"], M["navy"])
for col, heights in enumerate(CRATE_H):
    for row, h in enumerate(heights):
        for lvl in range(h):
            i = col * 6 + row * 3 + lvl
            jitter = ((i * 53) % 100) / 100.0 * 7.0 - 3.5
            box(entities, f"Revetment Crate {i}",
                (REV_X - 5.5 + col * 2.3, REV_TOP + lvl * 0.85, REV_Z - 4.0 + row * 1.5),
                (2.0, 0.8, 1.2), CRATE_MATS[(col + row + lvl) % 5],
                rot=yaw(jitter * (0.4 + 0.3 * lvl)), folder=F_REV)
# Drums on the open south side of the pocket, where they are not buried behind the stack.
for i in range(6):
    shape(entities, f"Revetment Drum {i}", (REV_X - 7.4 + (i % 3) * 1.0, REV_TOP + 0.5, REV_Z - 6.0 + (i // 3) * 1.0),
          cylinder_params(0.42, 1.0, 26), M["hazard"] if i % 2 else M["container_red"], folder=F_REV)

# One lamp inside the U -- the only direct light reaching a three-sided pocket.
box(entities, "Revetment Lamp Mast", (REV_X - 0.12, REV_TOP, REV_Z + 3.38), (0.24, 4.5, 0.24),
    M["galv"], folder=F_REV)
e = light(entities, "Revetment Lamp", (REV_X, REV_TOP + 4.6, REV_Z + 3.5),
          face_dir(0.0, -1.0, 0.0), folder=F_REV)
add_sphere_light(e, color=(1.0, 0.88, 0.70), intensity=95.0, radius=0.16, draw_range=18.0,
                 draw_emissive=True)

# The pocket is a distinct environment -- three 2.2m barriers and one lamp -- but it is open
# to the south and to the sky, so it cannot be treated as sealed. Bounds overshoot 0.6 into
# the barriers on the three closed sides, which is exactly the fadeMargin, so the band ends
# up buried inside them; on the open south face and the top that same band fades out into the
# yard probe instead of ending on a hard seam. Capture sits north of the crate stack, below
# the lamp proxy.
probe_box(entities, "Probe Revetment", (REV_X - 7.2, GRADE - 0.6, REV_Z - 7.2),
          (REV_X + 7.2, GRADE + 5.0, REV_Z + 7.2), capture=(0.0, 0.4, 2.0),
          fade=0.6, folder=F_REV)


# =============================================================================
# radar mast -- small bright emitters at distance
# =============================================================================
F_MAST = folder_entity(folders, "Radar Mast")

MX, MZ, MAST_H, LEG = -6.0, 38.0, 18.0, 1.6
MAST_Y0 = GRADE + 0.9
box(entities, "Mast Base", (MX - 2.4, GRADE, MZ - 2.4), (4.8, 0.9, 4.8), M["concrete"], folder=F_MAST)
for i, (ox, oz) in enumerate(((-LEG, -LEG), (LEG, -LEG), (LEG, LEG), (-LEG, LEG))):
    shape(entities, f"Mast Leg {i}", (MX + ox, MAST_Y0 + MAST_H * 0.5, MZ + oz),
          cylinder_params(0.16, MAST_H, 16), M["galv"], folder=F_MAST)
CORNERS = ((-LEG, -LEG), (LEG, -LEG), (LEG, LEG), (-LEG, LEG))
for level in range(6):
    ly = MAST_Y0 + 1.0 + level * 2.8
    for i in range(4):
        ax, az = CORNERS[i]
        bx, bz = CORNERS[(i + 1) % 4]
        platform(entities, f"Mast Brace {level}-{i}", (MX + ax, ly, MZ + az), (MX + bx, ly, MZ + bz),
                 0.14, M["galv"], thickness=0.12, folder=F_MAST)
        platform(entities, f"Mast Diagonal {level}-{i}", (MX + ax, ly, MZ + az),
                 (MX + bx, ly + 2.8, MZ + bz), 0.11, M["galv"], thickness=0.10, folder=F_MAST)

# Bowl opens along local +Y with its base at y=0, so pitch it back to aim at the horizon.
# Its effective outer radius is flatRadius + curveRadius (2.2 here), NOT the `radius` arg,
# which only clamps flatRadius -- so it is mounted on a pylon high enough that the pitched
# rim clears the head and the top lattice braces rather than growing through them.
HEAD_TOP = MAST_Y0 + MAST_H + 0.6
DISH_Y = MAST_Y0 + MAST_H + 3.0
box(entities, "Mast Head", (MX - 1.0, MAST_Y0 + MAST_H, MZ - 1.0), (2.0, 0.6, 2.0), M["steel"], folder=F_MAST)
shape(entities, "Mast Dish Pylon", (MX, (HEAD_TOP + DISH_Y) * 0.5, MZ),
      cylinder_params(0.3, DISH_Y - HEAD_TOP, 18), M["steel"], folder=F_MAST)
shape(entities, "Mast Dish", (MX, DISH_Y, MZ + 1.2),
      bowl_params(2.4, 1.2, 1.2, 1.0, 20, 48, 0.05), M["marking"], rot=pitch(-62.0), folder=F_MAST)

for i, by in enumerate((MAST_Y0 + 5.0, MAST_Y0 + 10.5, DISH_Y + 3.1)):
    e = light(entities, f"Mast Beacon {i}", (MX, by, MZ), folder=F_MAST)
    add_sphere_light(e, color=(1.0, 0.08, 0.03), intensity=55.0, radius=0.22, draw_range=34.0,
                     draw_emissive=True)

# Thin verticals: a deliberately hostile case for thin-geometry shadowing. Set behind the
# dish in -Z, where the pitched rim does not reach.
for i in range(4):
    shape(entities, f"Mast Whip {i}", (MX - 0.7 + i * 0.45, HEAD_TOP + 1.8, MZ - 1.7),
          cylinder_params(0.045, 3.6, 12), M["alu"], folder=F_MAST)


# =============================================================================
# watchtowers
# =============================================================================
F_TOWER = folder_entity(folders, "Watchtowers")


def watchtower(tag, cx, cz, facing_deg):
    """Open steel frame with a spiral stair inside and a roofed deck at 7m. The parapet
    leaves the +X side open; the searchlight clears it at 8.9m regardless of aim."""
    pad_top = GRADE + 0.3
    deck_y = pad_top + 6.9
    box(entities, f"{tag} Pad", (cx - 3.2, GRADE, cz - 3.2), (6.4, 0.3, 6.4), M["concrete"], folder=F_TOWER)
    LEG_POS = ((-2.6, -2.6), (2.4, -2.6), (2.4, 2.4), (-2.6, 2.4))
    for i, (ox, oz) in enumerate(LEG_POS):
        box(entities, f"{tag} Leg {i}", (cx + ox, pad_top, cz + oz), (0.30, deck_y - pad_top, 0.30),
            M["galv"], folder=F_TOWER)
    # X-bracing between leg centres, two panels per face, same platform trick as the mast.
    LEG_C = ((-2.45, -2.45), (2.55, -2.45), (2.55, 2.55), (-2.45, 2.55))
    for lv in range(2):
        by = pad_top + 0.4 + lv * 3.2
        for i in range(4):
            ax, az = LEG_C[i]
            bx, bz = LEG_C[(i + 1) % 4]
            platform(entities, f"{tag} Brace {lv}-{i}A", (cx + ax, by, cz + az),
                     (cx + bx, by + 2.6, cz + bz), 0.12, M["galv"], thickness=0.1, folder=F_TOWER)
            platform(entities, f"{tag} Brace {lv}-{i}B", (cx + ax, by + 2.6, cz + az),
                     (cx + bx, by, cz + bz), 0.12, M["galv"], thickness=0.1, folder=F_TOWER)
    # SpiralStaircase is CENTRE pivot in XZ, helix rising from y=0 about local Y.
    shape(entities, f"{tag} Stair", (cx, pad_top, cz),
          spiral_params(28, deck_y - pad_top, 2.3, 0.35, 0.14, 620.0, 12, True, False),
          M["steel"], folder=F_TOWER, friction=0.8)
    box(entities, f"{tag} Deck", (cx - 3.0, deck_y, cz - 3.0), (6.0, 0.25, 6.0), M["concrete"], folder=F_TOWER)
    par_y = deck_y + 0.25
    # The west run fills only the gap between the south and north runs -- overlapping
    # them at the corners would leave coplanar co-facing top and side faces.
    for i, (px, pz, sx, sz) in enumerate(((-3.0, -3.0, 6.0, 0.2), (-3.0, 2.8, 6.0, 0.2),
                                          (-3.0, -2.8, 0.2, 5.6))):
        box(entities, f"{tag} Parapet {i}", (cx + px, par_y, cz + pz), (sx, 1.05, sz),
            M["concrete_dark"], folder=F_TOWER)
    for i, (ox, oz) in enumerate(((-2.9, -2.9), (2.7, -2.9), (2.7, 2.7), (-2.9, 2.7))):
        box(entities, f"{tag} Roof Post {i}", (cx + ox, par_y, cz + oz), (0.18, 2.4, 0.18),
            M["galv"], folder=F_TOWER)
    roof_y = par_y + 2.4
    box(entities, f"{tag} Roof", (cx - 3.3, roof_y, cz - 3.3), (6.6, 0.22, 6.6), M["steel"], folder=F_TOWER)
    e = light(entities, f"{tag} Lamp", (cx, roof_y - 0.3, cz), face_dir(0.0, -1.0, 0.0), folder=F_TOWER)
    add_sphere_light(e, color=(1.0, 0.90, 0.74), intensity=60.0, radius=0.13, draw_range=16.0,
                     draw_emissive=True)
    # Narrow aimed area light: the sharpest DI edge in the level.
    ax, az = math.cos(math.radians(facing_deg)), math.sin(math.radians(facing_deg))
    # Just a post: the emissive proxy read as a panel floating under the roof, but a housing
    # box behind it only silhouetted itself against the glow from anywhere the beam points.
    box(entities, f"{tag} Searchlight Post", (cx + ax * 2.35, par_y, cz + az * 2.35), (0.1, 1.35, 0.1),
        M["galv"], folder=F_TOWER)
    e = light(entities, f"{tag} Searchlight", (cx + ax * 2.4, par_y + 1.4, cz + az * 2.4),
              face_dir(ax, -0.34, az), folder=F_TOWER)
    add_area_light(e, color=(1.0, 0.96, 0.88), intensity=420.0, half_width=0.5, half_height=0.5,
                   draw_range=46.0, draw_emissive=True)


watchtower("Tower SW", -50.0, -40.0, 35.0)
watchtower("Tower NE", 50.0, 44.0, 215.0)   # north of the helipad disc, inboard of both walls


# =============================================================================
# motor pool -- open-ground sun shadows
# =============================================================================
F_POOL = folder_entity(folders, "Motor Pool")

CONT = (6.06, 2.59, 2.44)
for s, (bx, bz, ang) in enumerate(((-14.0, -30.0, 0.0), (-14.0, -35.0, 0.0),
                                   (2.0, -30.0, 90.0), (6.0, -22.0, 18.0))):
    for lvl in range(2 if s % 2 == 0 else 1):
        by = GRADE + lvl * (CONT[1] + 0.06)
        cmat = [M["olive"], M["container_red"], M["navy"], M["od_light"]][(s + lvl) % 4]
        box(entities, f"Container {s}-{lvl}", (bx, by, bz), CONT, cmat, rot=yaw(ang), folder=F_POOL)
        # Corrugation ribs in the container's own colour -- contrast ribs read as planking.
        for r in range(7):
            rx, rz = yaw_offset(bx, bz, ang, 0.3 + r * 0.8, -0.05)
            vbox(entities, f"Container Rib {s}-{lvl}-{r}", (rx, by + 0.15, rz),
                 (0.12, CONT[1] - 0.3, 0.06), cmat, rot=yaw(ang), folder=F_POOL)
        # Door end: two vertical locking bars proud of the +X face.
        for d in (0, 1):
            dx_, dz_ = yaw_offset(bx, bz, ang, CONT[0] + 0.02, 0.7 + d * 1.0)
            vbox(entities, f"Container Bar {s}-{lvl}-{d}", (dx_, by + 0.2, dz_),
                 (0.05, CONT[1] - 0.4, 0.07), M["concrete_dark"], rot=yaw(ang), folder=F_POOL)

# Jersey barriers as a proper serpentine: three rows alternating sides of the road, each a
# run of butted barriers. Cross-section is a two-box trapezoid (wide foot, narrow top) --
# the wedge-pair version read as a row of dragon's teeth, which is a different obstacle
# entirely. Each barrier is 2.0 long, so a 4-unit row spans 8m.
JB_L, JB_FOOT, JB_TOP = 2.0, 0.62, 0.34
for r, (row_x0, row_z) in enumerate(((-8.0, -44.0), (0.4, -40.0), (-8.0, -36.0))):
    for k in range(4):
        # 0.16 gap between units: butted end to end they merged into one unbroken slab.
        x0 = row_x0 + k * (JB_L + 0.16)
        box(entities, f"Jersey {r}-{k} Foot", (x0, GRADE, row_z), (JB_L, 0.34, JB_FOOT),
            M["concrete"], folder=F_POOL)
        box(entities, f"Jersey {r}-{k} Body", (x0, GRADE + 0.34, row_z + (JB_FOOT - JB_TOP) * 0.5),
            (JB_L, 0.56, JB_TOP), M["concrete"], folder=F_POOL)

# Fuel farm in the south-east corner: bunded pad, three tanks on cradles, one shared
# manifold run with a riser per tank. Moved out of the hangar approach it used to block.
FF_X0, FF_Z0 = 30.0, -45.0                 # pad corner; pad is 14 x 16
box(entities, "Fuel Farm Pad", (FF_X0, GRADE - 0.02, FF_Z0), (14.0, 0.1, 16.0),
    M["concrete"], folder=F_POOL, friction=0.9)
for name, corner, size in (
        ("Fuel Bund West", (FF_X0, GRADE + 0.08, FF_Z0), (0.4, 0.8, 16.0)),
        ("Fuel Bund East", (FF_X0 + 13.6, GRADE + 0.08, FF_Z0), (0.4, 0.8, 16.0)),
        ("Fuel Bund South", (FF_X0 + 0.4, GRADE + 0.08, FF_Z0), (13.2, 0.8, 0.4))):
    box(entities, name, corner, size, M["concrete_dark"], folder=F_POOL)
for i in range(3):
    tz = FF_Z0 + 3.4 + i * 5.0
    shape(entities, f"Fuel Tank {i}", (FF_X0 + 6.4, GRADE + 2.4, tz), capsule_params(2.0, 8.4, 36, 18),
          M["steel"], rot=roll(90.0), folder=F_POOL)
    for c in (0, 1):
        box(entities, f"Fuel Cradle {i}-{c}", (FF_X0 + 3.4 + c * 5.2, GRADE + 0.08, tz - 1.6),
            (1.0, 1.2, 3.2), M["concrete_dark"], folder=F_POOL)
    # Riser drops from the tank belly to the manifold run along the east side.
    shape(entities, f"Fuel Riser {i}", (FF_X0 + 11.6, GRADE + 0.75, tz),
          cylinder_params(0.09, 1.3, 14), M["galv"], folder=F_POOL)
shape(entities, "Fuel Manifold", (FF_X0 + 11.6, GRADE + 0.35, FF_Z0 + 6.9),
      cylinder_params(0.11, 11.0, 14), M["galv"], rot=pitch(90.0), folder=F_POOL)


# Repair bay on the south half of the hangar apron: a hull up on stands with its wheels
# off, plus the clutter that implies. Kept clear of z=-2, the door's sight line.
RB_X, RB_Z = -22.6, -8.6
# Chassis + bed + cab rather than one block: a lone box on stands read as an abstract slab.
box(entities, "Repair Chassis", (RB_X, GRADE + 0.85, RB_Z), (4.6, 0.28, 2.5), M["galv"], folder=F_POOL)
box(entities, "Repair Bed", (RB_X + 0.1, GRADE + 1.13, RB_Z + 0.05), (2.9, 0.95, 2.4),
    M["od_light"], folder=F_POOL)
box(entities, "Repair Cab", (RB_X + 3.15, GRADE + 1.13, RB_Z + 0.1), (1.4, 1.5, 2.3),
    M["olive"], folder=F_POOL)
vbox(entities, "Repair Windshield", (RB_X + 3.1, GRADE + 2.0, RB_Z + 0.2), (0.06, 0.55, 2.1),
     M["navy"], folder=F_POOL)
for i, (sx_, sz_) in enumerate(((0.4, 0.3), (3.7, 0.3), (0.4, 2.0), (3.7, 2.0))):
    box(entities, f"Repair Stand {i}", (RB_X + sx_, GRADE, RB_Z + sz_), (0.5, 0.85, 0.5),
        M["concrete_dark"], folder=F_POOL)
for i in range(2):
    shape(entities, f"Repair Wheel {i}", (RB_X + 6.2, GRADE + 0.24 + i * 0.42, RB_Z + 0.6),
          torus_params(0.62, 0.22, 32, 18), M["rubber"], rot=pitch(90.0), folder=F_POOL)
shape(entities, "Repair Wheel Leaning", (RB_X + 5.6, GRADE + 0.7, RB_Z + 2.6),
      torus_params(0.62, 0.22, 32, 18), M["rubber"], rot=roll(18.0), folder=F_POOL)
box(entities, "Repair Bench", (RB_X - 0.4, GRADE, RB_Z + 3.4), (2.6, 0.9, 0.8), M["galv"], folder=F_POOL)
for i in range(3):
    shape(entities, f"Repair Drum {i}", (RB_X + 2.6 + (i % 2) * 0.9, GRADE + 0.45, RB_Z + 3.5 + (i // 2) * 0.9),
          cylinder_params(0.4, 0.9, 26), M["hazard"] if i else M["olive"], folder=F_POOL)
vbox(entities, "Repair Tarp", (RB_X + 3.4, GRADE + 0.02, RB_Z - 1.6), (2.4, 0.04, 1.8),
     M["olive"], rot=yaw(9.0), folder=F_POOL)


def vehicle(tag, vx, vz, ang, hull=(6.4, 1.5, 3.0), turret=True):
    """Blocky mock-up. The turret overhang and wheel gaps are what make the sun shadow
    read as a vehicle rather than a crate. Every part's position routes through
    yaw_offset with the hull's pivot, so rotated vehicles stay assembled."""
    box(entities, f"{tag} Hull", (vx, GRADE + 0.72, vz), hull, M["olive"], rot=yaw(ang), folder=F_POOL)
    # Glacis butts the hull exactly rather than overlapping it: a 0.1 overlap would put
    # two coplanar top faces at the same height.
    gx, gz = yaw_offset(vx, vz, ang, -1.1, 0.0)
    box(entities, f"{tag} Glacis", (gx, GRADE + 0.72, gz), (1.1, 1.5, hull[2]), M["olive"],
        rot=yaw(ang), folder=F_POOL)
    if turret:
        tx, tz = yaw_offset(vx, vz, ang, hull[0] * 0.45, hull[2] * 0.5)
        shape(entities, f"{tag} Turret", (tx, GRADE + 2.64, tz),
              cylinder_params(1.25, 0.85, 28), M["olive"], rot=yaw(ang), folder=F_POOL)
        mx, mz = yaw_offset(vx, vz, ang, hull[0] * 0.45 + 1.0, hull[2] * 0.5 - 0.3)
        box(entities, f"{tag} Mantlet", (mx, GRADE + 2.3, mz), (0.7, 0.65, 0.6),
            M["olive"], rot=yaw(ang), folder=F_POOL)
        box(entities, f"{tag} Barrel", (tx, GRADE + 2.5, tz),
            (5.0, 0.26, 0.26), M["olive"], rot=yaw(ang), folder=F_POOL)
        ex_, ez_ = yaw_offset(vx, vz, ang, hull[0] * 0.45 + 5.0, hull[2] * 0.5 - 0.17)
        box(entities, f"{tag} Muzzle", (ex_, GRADE + 2.46, ez_), (0.55, 0.34, 0.34),
            M["olive"], rot=yaw(ang), folder=F_POOL)
    # Wheel axles run lateral (local +Z): pitch(90) stands the cylinder's axis sideways,
    # then the hull yaw. Three axles a side -- two left a 6m hull reading as a toy.
    for w in range(6):
        axle = w % 3
        wx, wz = yaw_offset(vx, vz, ang, 0.9 + axle * (hull[0] - 2.0) * 0.5,
                            0.15 if w < 3 else hull[2] - 0.15)
        shape(entities, f"{tag} Wheel {w}", (wx, GRADE + 0.62, wz), cylinder_params(0.62, 0.5, 26),
              M["rubber"], rot=qmul(yaw(ang), pitch(90.0)), folder=F_POOL)


vehicle("Vehicle A", -30.0, -34.0, 0.0)
vehicle("Vehicle B", -21.0, -38.0, 12.0, turret=False)
vehicle("Vehicle C", 12.0, -36.0, -25.0)

# Tyre stacks: Torus rings sit in the local XY plane, so pitch(90) lays them flat.
for s, (tx, tz) in enumerate(((-6.0, -18.0), (-3.0, -19.5), (16.0, -28.0))):
    for i in range(4):
        shape(entities, f"Tyre {s}-{i}", (tx, GRADE + 0.24 + i * 0.42, tz),
              torus_params(0.62, 0.22, 32, 18), M["rubber"], rot=pitch(90.0), folder=F_POOL)

# Razor-wire spools along the inside of the south wall, skipping the gate throat where a
# lone 1.6m ring right in the entry sightline read as a leaning tyre.
for i in range(7):
    sx = -40.0 + i * 12.0
    if abs(sx) < 10.0:
        continue
    shape(entities, f"Wire Spool {i}", (sx, GRADE + 0.75, -46.5),
          torus_params(0.6, 0.13, 28, 14), M["steel"], rot=yaw(90.0), folder=F_POOL)

# Sandbag emplacement: two courses of capsules laid tangentially around a firing position.
SB_X, SB_Z, SB_R = 26.0, 12.0, 4.2
box(entities, "Emplacement Floor", (SB_X - 3.6, GRADE, SB_Z - 3.6), (7.2, 0.12, 7.2),
    M["concrete"], folder=F_POOL)
for course in range(3):
    for i in range(22):
        a = i / 22.0 * math.tau + course * 0.07
        tang = a + math.pi * 0.5
        tx, tz = math.cos(tang), math.sin(tang)
        # A capsule's long axis is local +Y, so run_quat (which only aims local +X) left every
        # bag standing on end. Map +Y onto the tangent and +X onto world up instead.
        shape(entities, f"Sandbag {course}-{i}",
              (SB_X + math.cos(a) * SB_R, GRADE + 0.2 + course * 0.42, SB_Z + math.sin(a) * SB_R),
              capsule_params(0.2, 0.78, 16, 8), M["sand"],
              rot=mat_to_quat((0.0, 1.0, 0.0), (tx, 0.0, tz), (tz, 0.0, -tx)), folder=F_POOL)

# Floodlight masts around the yard.
# The last mast covers the fuel farm, which sits in the corner walls' shade at this sun.
for i, (fx, fz, aim) in enumerate(((-20.0, -12.0, 300.0), (4.0, -14.0, 240.0),
                                   (18.0, 20.0, 150.0), (-38.0, 16.0, 20.0), (46.0, -40.0, 162.0))):
    box(entities, f"Floodlight Base {i}", (fx - 0.45, GRADE, fz - 0.45), (0.9, 0.5, 0.9),
        M["concrete"], folder=F_LIGHT)
    box(entities, f"Floodlight Mast {i}", (fx - 0.16, GRADE + 0.5, fz - 0.16), (0.32, 8.5, 0.32),
        M["galv"], folder=F_LIGHT)
    ax, az = math.cos(math.radians(aim)), math.sin(math.radians(aim))
    hx, hz = fx + ax * 0.6, fz + az * 0.6
    # Housing is plain steel; the light's own proxy quad is the lit element.
    visual(entities, f"Floodlight Housing {i}", (hx, GRADE + 9.0, hz), box_params(1.1, 0.5, 0.28),
           M["galv"], rot=yaw(-aim), folder=F_LIGHT)
    e = light(entities, f"Floodlight {i}", (hx, GRADE + 8.8, hz), face_dir(ax, -0.62, az), folder=F_LIGHT)
    add_area_light(e, color=(1.0, 0.93, 0.80), intensity=300.0, half_width=0.55, half_height=0.25,
                   draw_range=34.0, draw_emissive=True)


# =============================================================================
# barracks row + supply drop -- fills the bare west and north yard
# =============================================================================
F_BARRACKS = folder_entity(folders, "Barracks")

HUT_W, HUT_D, HUT_H = 9.0, 4.0, 2.7
# North of z=30: the hangar is 10m tall and the sun sits at ~25 degrees, so its shadow
# reaches z~27 here. The row used to sit inside it and read as three black boxes.
for h, (hz0, wall_mat) in enumerate(((30.0, M["od_light"]), (37.5, M["panel"]), (45.0, M["od_light"]))):
    hx0 = -57.0
    n = f"Hut {h}"
    box(entities, f"{n} Floor", (hx0, GRADE, hz0), (HUT_W, 0.15, HUT_D), M["floor"], folder=F_BARRACKS)
    box(entities, f"{n} Wall West", (hx0, GRADE + 0.15, hz0), (0.15, HUT_H, HUT_D), wall_mat, folder=F_BARRACKS)
    box(entities, f"{n} Wall South", (hx0 + 0.15, GRADE + 0.15, hz0), (HUT_W - 0.3, HUT_H, 0.15),
        wall_mat, folder=F_BARRACKS)
    box(entities, f"{n} Wall North", (hx0 + 0.15, GRADE + 0.15, hz0 + HUT_D - 0.15), (HUT_W - 0.3, HUT_H, 0.15),
        wall_mat, folder=F_BARRACKS)
    wall_run(entities, f"{n} Front", (hx0 + HUT_W, hz0), (hx0 + HUT_W, hz0 + HUT_D), HUT_H, 0.15,
             wall_mat, openings=[(0.5, 0.0, 0.9, 2.1), (1.8, 1.0, 1.0, 0.9)],
             base_y=GRADE + 0.15, folder=F_BARRACKS)
    # Windows down the long side; one door and one window over a 9m run read as a blank shed.
    for w in range(3):
        wx = hx0 + 1.6 + w * 2.5
        vbox(entities, f"{n} Window {w}", (wx, GRADE + 1.25, hz0 - 0.02), (1.1, 0.85, 0.05),
             M["navy"], folder=F_BARRACKS)
        vbox(entities, f"{n} Window Sill {w}", (wx - 0.1, GRADE + 1.18, hz0 - 0.07),
             (1.3, 0.07, 0.1), M["galv"], folder=F_BARRACKS)
    # Door awning on light posts, the one bit of relief on the entry face.
    vbox(entities, f"{n} Awning", (hx0 + HUT_W - 0.05, GRADE + 2.35, hz0 + 0.1), (1.3, 0.08, 1.7),
         M["galv"], folder=F_BARRACKS)
    for p in (0, 1):
        vbox(entities, f"{n} Awning Post {p}", (hx0 + HUT_W + 1.1, GRADE + 0.15, hz0 + 0.2 + p * 1.4),
             (0.08, 2.2, 0.08), M["galv"], folder=F_BARRACKS)
    box(entities, f"{n} Roof", (hx0 - 0.25, GRADE + 0.15 + HUT_H, hz0 - 0.25),
        (HUT_W + 0.5, 0.16, HUT_D + 0.5), M["galv"], folder=F_BARRACKS)
    box(entities, f"{n} Step", (hx0 + HUT_W, GRADE, hz0 + 0.5), (0.7, 0.15, 1.1), M["concrete"], folder=F_BARRACKS)
    box(entities, f"{n} AC Unit", (hx0 + HUT_W + 0.02, GRADE + 0.4, hz0 + 2.7), (0.5, 0.7, 0.8),
        M["galv"], folder=F_BARRACKS)
    e = light(entities, f"{n} Light", (hx0 + HUT_W * 0.5, GRADE + HUT_H - 0.2, hz0 + HUT_D * 0.5),
              folder=F_BARRACKS)
    add_sphere_light(e, color=(1.0, 0.93, 0.80), intensity=34.0, radius=0.09, draw_range=10.0,
                     draw_emissive=True)

# Pallet stacks inside the north gate, staged like an unloaded supply drop.
for p, (px, pz, cnt, topped) in enumerate(((3.0, 43.0, 6, True), (5.0, 45.0, 4, False),
                                           (7.4, 43.5, 8, True), (9.4, 45.6, 3, False))):
    for lv in range(cnt):
        vbox(entities, f"Pallet {p}-{lv}", (px, GRADE + lv * 0.145, pz), (1.25, 0.13, 1.05),
             M["od_light"], rot=yaw(((p * 31 + lv * 17) % 10) - 5.0), folder=F_BARRACKS)
    if topped:
        box(entities, f"Pallet Crate {p}", (px + 0.05, GRADE + cnt * 0.145, pz + 0.02),
            (1.1, 0.75, 0.95), M["olive"], folder=F_BARRACKS)


# =============================================================================
# gameplay
# =============================================================================
F_GAME = folder_entity(folders, "Gameplay")

spawn = base_entity("Spawn", (0.0, GRADE + 1.6, -45.5), folder_id=F_GAME)
spawn[SPAWN] = {"offset": [0.0, 0.0, 0.0], "priority": 1}
entities.append(spawn)

for name, pos, prio in (("Checkpoint Hangar", (HX1 - 3.0, HFY + 1.2, HELI_Z + 6.0), 1),
                        ("Checkpoint Bunker", (DOOR_CX, BFY + 1.2, BZ1 - 3.0), 2),
                        ("Checkpoint Helipad", (PAD_X, PAD_TOP + 1.2, PAD_Z), 3)):
    e = base_entity(name, pos, folder_id=F_GAME)
    add_checkpoint(e, name_id(name), priority=prio, spawn_offset=(0.0, 0.6, 0.0))
    entities.append(e)

# Fallback for open ground: the largest volume in the level, so smallest-volume-wins leaves
# every interior to its own tighter probe. It is the one probe that does span occluding walls,
# which is unavoidable for a catch-all -- the fix is that anything with a genuinely different
# lighting environment (hangar, bunker, culvert, revetment, helipad) has its own. Top is above
# the radar mast so nothing tall falls outside every probe. fadeMargin 0: nothing encloses it
# to fade into, and a band here would dim the perimeter walls sitting on the boundary.
probe_box(entities, "Probe Yard", (GX0, GRADE, GZ0), (GX1, 26.0, GZ1),
          capture=(0.0, 8.0, -30.0), folder=F_SITE)


# =============================================================================
# write
# =============================================================================
all_entities = folders + entities
# Editor camera: outside the gate, looking into the base. camera_look_quat, NOT face_dir --
# the camera's forward axis is -Z, so a face_dir quat here looks the opposite way.
CAM_POS = (24.0, 15.0, -64.0)
CAMERA = {"rotation": list(camera_look_quat(-24.0, -13.0, 64.0)), "translation": list(CAM_POS)}
wa.write_scene(SCENE_PATH, all_entities, SCENE_ID, SCENE_NAME, editor_camera=CAMERA)

# Capture-run shots, one or two per zone; names order the PNG listing into a walkthrough.
SHOTS_PATH = os.path.join(REPO, "assets", "scenes", "military_sandbox.wshots")
write_shots(SHOTS_PATH, [
    shot("01_overview", CAM_POS, (0.0, 2.0, 0.0)),
    shot("02_gate", (0.0, 1.8, -58.0), (0.0, 3.0, -40.0)),
    shot("03_motorpool_shadows", (-2.0, 8.0, -18.0), (-22.0, 1.0, -32.0)),
    shot("04_hangar_door", (-10.0, 4.0, -2.0), (-40.0, 3.0, -2.0)),
    shot("05_hangar_red_wall", (-27.0, 5.0, 5.0), (-51.0, 4.0, 1.0)),
    shot("06_hangar_interior", (-26.0, 5.0, 7.0), (-48.0, 2.5, -8.0)),
    shot("07_bunker_console", (BX1 - 1.5, 2.0, BZ1 - 1.0), (BX0 + 3.0, 1.3, BZ0 + 3.0)),
    shot("08_bunker_strip", (BX0 + 2.0, 1.8, BZ1 - 2.0), (BX1, 2.4, BZ0 + 5.0)),
    shot("09_culvert_mouth", (CULV_X, CULV_Y, CULV_Z0 - 4.0), (CULV_X, CULV_Y, CULV_Z0 + 10.0)),
    # Set back from the lamp rather than level with it: standing on the lamp looking away
    # framed only unlit bore, which shows the falloff test nothing.
    shot("10_culvert_middle", (CULV_X, CULV_Y, CULV_Z0 + 4.0), (CULV_X, CULV_Y - 0.1, CULV_Z0 + 28.0)),
    shot("11_helipad", (20.0, 9.0, 46.0), (PAD_X, 1.0, PAD_Z)),
    shot("12_revetment", (REV_X, 3.0, REV_Z - 12.0), (REV_X, 1.0, REV_Z + 2.0)),
    shot("13_radar_mast", (-14.0, 3.0, 26.0), (MX, 12.0, MZ)),
    shot("14_tower_searchlight", (-40.0, 6.0, -33.0), (-50.0, 8.0, -40.0)),
    shot("15_fuel_farm", (52.0, 5.0, -22.0), (FF_X0 + 6.0, 1.5, FF_Z0 + 8.0)),
    shot("16_barracks", (-44.0, 6.0, 22.0), (-53.0, 1.5, 38.0)),
    shot("17_heli", (-28.0, 3.4, 3.5), (HELI_X, 2.2, HELI_Z - 0.5)),
])

print(f"wrote {SCENE_PATH}")
print(f"  {len(entities)} entities + {len(folders)} folders, scene id {SCENE_ID}")
print(f"  {len(M)} materials -> assets/materials")
print(f"  shots -> {SHOTS_PATH}")
