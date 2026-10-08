"""Canonical v2 text-body emitters for .wscene/.wprefab (docs/serialization/text_format.md).

Mirrors the engine's reflected serializer (src/engine/reflection/reflection_serialize.h,
docs/engine/reflection.md): per-component field lists below copy each WILL_REFLECT declaration,
fields equal to the struct default are omitted, nested structs are blocks, variants are
`key|<index>` plus the alternative's fields inline, containers are `key|<count>` plus one `item`
block per element. Scene bodies are built from the dict shapes wscene_authoring.py produces;
the adapters at the bottom map those onto the reflected layout.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "core", "tools"))
from xxh3 import string_id


def type_key(s):
    """Decimal block opener for a component: its COMPONENT_NAME hashed the way Game::TypeSID does."""
    return str(string_id(s))


def hx(v):
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", float(v)))[0]


def f32(v):
    return struct.unpack("<f", struct.pack("<f", float(v)))[0]


def bstr(v):
    return "1" if v else "0"


def esc(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r")


class W:
    def __init__(self):
        self.lines = []

    def key(self, k, *vals):
        self.lines.append(k + "|" + "|".join(str(v) for v in vals))

    def key_f(self, k, *vals):
        self.lines.append(k + "|" + "|".join(hx(v) for v in vals))

    def key_str(self, k, s):
        self.lines.append(k + "|" + esc(s))

    def begin(self, tok):
        self.lines.append(str(tok))

    def end(self):
        self.lines.append(";")

    def text(self):
        return "\n".join(self.lines) + "\n" if self.lines else ""


# ---- reflected field types ----
# Leaf kinds: "f" float, "i" int32, "u" unsigned / enum / id, "b" bool, "s" string,
# "v2"/"v3"/"v4" float vectors, "q" quat [w,x,y,z]. Composite kinds are tuples:
# ("struct", fields), ("variant", [fields-or-None per index]), ("list", elem), ("array", elem, n).
# A field is (key, type, default); a struct's default is {} (every field at its own default).

VEC_LEN = {"v2": 2, "v3": 3, "v4": 4, "q": 4}


def canon(t, v):
    if isinstance(t, tuple):
        kind = t[0]
        if kind == "struct":
            v = v or {}
            return tuple(canon(ft, v.get(k, d)) for k, ft, d in t[1])
        if kind == "variant":
            index, fields = v
            alt = t[1][index]
            return (int(index), canon(("struct", alt), fields) if alt is not None else None)
        if kind == "list":
            return tuple(canon(t[1], e) for e in v)
        if kind == "array":
            elems = list(v) + [elem_default(t[1])] * (t[2] - len(v))
            return tuple(canon(t[1], e) for e in elems)
    if t == "f":
        return f32(v)
    if t in ("i", "u"):
        return int(v)
    if t == "b":
        return bool(v)
    if t == "s":
        return str(v)
    return tuple(f32(x) for x in v)


def elem_default(t):
    if isinstance(t, tuple):
        if t[0] == "struct":
            return {}
        if t[0] in ("list", "array"):
            return []
        raise ValueError("no implicit default for " + t[0])
    return {"f": 0.0, "i": 0, "u": 0, "b": False, "s": "", "v2": [0.0] * 2, "v3": [0.0] * 3, "v4": [0.0] * 4}[t]


def write_value(w, k, t, v):
    if isinstance(t, tuple):
        kind = t[0]
        if kind == "struct":
            w.begin(k)
            write_fields(w, t[1], v)
            w.end()
        elif kind == "variant":
            index, fields = v
            w.key(k, int(index))
            if t[1][index] is not None:
                write_fields(w, t[1][index], fields)
        else:
            elems = list(v)
            if kind == "array":
                elems += [elem_default(t[1])] * (t[2] - len(elems))
                while elems and canon(t[1], elems[-1]) == canon(t[1], elem_default(t[1])):
                    elems.pop()
            w.key(k, len(elems))
            for e in elems:
                w.begin("item")
                if isinstance(t[1], tuple) and t[1][0] == "struct":
                    write_fields(w, t[1][1], e)
                else:
                    write_value(w, "v", t[1], e)
                w.end()
    elif t == "f":
        w.key_f(k, v)
    elif t in ("i", "u"):
        w.key(k, int(v))
    elif t == "b":
        w.key(k, bstr(v))
    elif t == "s":
        w.key_str(k, v)
    else:
        vals = [float(x) for x in v]
        if len(vals) != VEC_LEN[t]:
            raise ValueError(f"{k}: expected {VEC_LEN[t]} components, got {len(vals)}")
        w.key_f(k, *vals)


def write_fields(w, fields, v):
    for k, t, d in fields:
        val = v.get(k, d)
        if canon(t, val) == canon(t, d):
            continue
        write_value(w, k, t, val)


def S(fields):
    return ("struct", fields)


# ---- value types (model_types.h, spline.h, physics_body_desc.h, path_mover_component.h) ----

SPLINE_POINT = [("pos", "v3", [0.0, 0.0, 0.0]), ("roll", "f", 0.0)]
SPLINE = [("mode", "u", 1), ("bClosed", "b", False), ("points", ("list", S(SPLINE_POINT)), [])]

SPLINE_PROFILE = [("type", "u", 0), ("width", "f", 0.4), ("height", "f", 0.4), ("cornerRadius", "f", 0.08),
                  ("cornerSegments", "i", 3), ("thickness", "f", 0.05)]
SPLINE_RAILING = [("bEnabled", "b", False), ("lanes", ("list", "v2"), []), ("bPosts", "b", True), ("postInterval", "i", 4),
                  ("postBottom", "f", 0.0), ("postTop", "f", 1.0), ("postSize", "v2", [0.05, 0.05]), ("postLateral", "f", 0.0),
                  ("lateralOffset", "f", 0.0)]
SPLINE_PARAMS = [("spline", S(SPLINE), {}), ("radius", "f", 0.5), ("rollAngle", "f", 0.0), ("sides", "i", 8), ("segmentsPerSpan", "i", 8),
                 ("bCaps", "b", True), ("bCrossPlanks", "b", False), ("crossPlankInterval", "i", 4), ("crossPlankHeight", "f", 0.0),
                 ("crossPlankThickness", "f", 0.1), ("crossPlankLength", "f", 0.3), ("profile", S(SPLINE_PROFILE), {}),
                 ("railing", S(SPLINE_RAILING), {})]

WALL_OPENING = [("x", "f", 0.0), ("y", "f", 0.0), ("w", "f", 0.0), ("h", "f", 0.0)]

# Engine::ProceduralParams alternatives, by variant index (0 = monostate).
PROC_ALTS = [
    None,
    [("stepCount", "i", 10), ("width", "f", 1.0), ("totalDepth", "f", 3.0), ("totalHeight", "f", 2.0), ("bSpecifyStepHeight", "b", False),
     ("stepHeight", "f", 0.2), ("bIsClosed", "b", True)],
    [("sizeX", "f", 1.0), ("sizeY", "f", 1.0), ("sizeZ", "f", 1.0), ("chamferX", "v4", [0.0] * 4), ("chamferY", "v4", [0.0] * 4),
     ("chamferZ", "v4", [0.0] * 4)],
    [("radius", "f", 0.5), ("height", "f", 2.0), ("slices", "i", 16), ("bCapped", "b", True)],
    [("radius", "f", 0.5), ("height", "f", 2.0), ("slices", "i", 16), ("rings", "i", 8)],
    [("ringRadius", "f", 1.0), ("tubeRadius", "f", 0.25), ("slices", "i", 16), ("stacks", "i", 16)],
    [("width", "f", 2.0), ("height", "f", 2.5), ("depth", "f", 0.5), ("thickness", "f", 0.3), ("sides", "i", 8), ("bFillCorners", "b", False)],
    [("sizeX", "f", 1.0), ("sizeY", "f", 1.0), ("sizeZ", "f", 1.0)],
    [("radius", "f", 0.5), ("height", "f", 2.0), ("slices", "i", 16), ("bCapped", "b", True)],
    [("width", "f", 1.0), ("height", "f", 2.0), ("depth", "f", 0.05), ("archHeight", "f", 0.5), ("gap", "f", 0.0), ("sides", "i", 8),
     ("bHalf", "b", False), ("bFlip", "b", False)],
    [("sizeX", "f", 2.0), ("sizeZ", "f", 2.0), ("tilesX", "i", 1), ("tilesZ", "i", 1)],
    [("radius", "f", 0.5), ("slices", "i", 16), ("stacks", "i", 8)],
    [("radius", "f", 0.5), ("subdivisions", "i", 3)],
    [("radius", "f", 0.5), ("slices", "i", 16), ("stacks", "i", 8)],
    [("outerRadius", "f", 0.5), ("innerRadius", "f", 0.3), ("height", "f", 2.0), ("slices", "i", 16)],
    [("radius", "f", 0.5)],
    [("radius", "f", 0.5)],
    [("radius", "f", 0.5)],
    [("radius", "f", 0.5)],
    [("scale", "f", 1.0), ("slices", "i", 8), ("stacks", "i", 8)],
    [("scale", "f", 1.0), ("tubeRadius", "f", 1.0), ("slices", "i", 16), ("stacks", "i", 128)],
    [("width", "f", 2.0), ("height", "f", 2.0), ("radius", "f", 2.0), ("segments", "i", 8), ("bHalfPipe", "b", False), ("flatLength", "f", 1.0),
     ("lipHeight", "f", 0.02)],
    [("radius", "f", 2.0), ("height", "f", 2.0), ("curveRadius", "f", 2.0), ("flatRadius", "f", 0.0), ("lipHeight", "f", 0.02),
     ("slices", "i", 16), ("segments", "i", 8)],
    [("stepCount", "i", 12), ("stepHeight", "f", 0.2), ("totalHeight", "f", 2.4), ("bSpecifyStepHeight", "b", False), ("outerRadius", "f", 1.5),
     ("centerColumnRadius", "f", 0.25), ("treadThickness", "f", 0.08), ("degreesPerStep", "f", 30.0), ("totalSweep", "f", 360.0),
     ("bSpecifyDegreesPerStep", "b", False), ("arcSegments", "i", 6), ("bShowCenterColumn", "b", True), ("bRamp", "b", False)],
    [("outerRadius", "f", 0.5), ("innerRadius", "f", 0.25), ("slices", "i", 32), ("bDoubleSided", "b", True)],
    [("sizeX", "f", 4.0), ("sizeY", "f", 3.0), ("sizeZ", "f", 0.2), ("openingCount", "i", 0), ("openings", ("array", S(WALL_OPENING), 8), [])],
    [("sizeX", "f", 0.5), ("sizeY", "f", 3.0), ("sizeZ", "f", 0.5), ("chordSize", "f", 0.06), ("braceSize", "f", 0.04), ("bayCount", "i", 4),
     ("pattern", "i", 0)],
    [("sizeX", "f", 2.4), ("sizeY", "f", 2.4), ("sizeZ", "f", 0.05), ("ribDepth", "f", 0.05), ("ribWidth", "f", 0.2), ("ribCount", "i", 6)],
]
PROC = ("variant", PROC_ALTS)
NO_PROC = (0, {})

MODULE_PART = [("type", PROC, NO_PROC), ("offset", "v3", [0.0, 0.0, 0.0]), ("rotation", "q", [1.0, 0.0, 0.0, 0.0]), ("slot", "i", 0)]
MODULE_PARAMS = [("parts", ("list", S(MODULE_PART)), [])]

TEXT3D_SOURCE = [("fontId", "u", 0), ("text", "s", ""), ("depth", "f", 0.2), ("flatness", "f", 0.005), ("tracking", "f", 0.0), ("scale", "f", 1.0),
                 ("wrapWidth", "f", 0.0), ("bendRadius", "f", 0.0), ("smoothNormals", "b", True), ("align", "u", 0), ("anchor", "u", 0),
                 ("precise", "b", False)]

SHAPE_ALTS = [
    [("halfExtents", "v3", [0.5, 0.5, 0.5])],
    [("radius", "f", 0.5)],
    [("radius", "f", 0.5), ("halfHeight", "f", 0.5)],
    [("meshSourceModelId", "u", 0), ("meshPrecise", "b", False), ("proceduralType", PROC, NO_PROC), ("splineParams", S(SPLINE_PARAMS), {}),
     ("text3DSource", S(TEXT3D_SOURCE), {})],
]
PHYSICS_SHAPE = [("type", ("variant", SHAPE_ALTS), (0, {})), ("offset", "v3", [0.0, 0.0, 0.0]), ("rotation", "q", [1.0, 0.0, 0.0, 0.0]),
                 ("bakedScale", "v3", [1.0, 1.0, 1.0])]

PATH_POINT_SETTINGS = [("rotation", "q", [1.0, 0.0, 0.0, 0.0]), ("easing", "u", 0), ("speed", "f", 1.0), ("waitTime", "f", 0.0)]

RENDER_TAIL = [("renderOffset", "v3", [0.0, 0.0, 0.0]), ("renderRotation", "q", [1.0, 0.0, 0.0, 0.0])]

# ---- components (WILL_REFLECT field order) ----

FIELDS = {
    "TransformComponent": [("translation", "v3", [0.0, 0.0, 0.0]), ("rotation", "q", [1.0, 0.0, 0.0, 0.0]), ("scale", "v3", [1.0, 1.0, 1.0])],
    "HierarchyComponent": [("parentStableId", "u", 0)],
    "NameComponent": [("name", "s", "")],
    "PrefabInstanceComponent": [("prefabId", "u", 0), ("bMasterPrefab", "b", False)],
    "StableIdComponent": [("id", "u", 0), ("sortOrder", "u", 0)],
    "EntityFolderComponent": [("folderId", "u", 0)],
    "SceneFolderComponent": [("folderId", "u", 0), ("parentFolder", "u", 0), ("name", "s", "")],
    "FreeCameraComponent": [("moveSpeed", "f", 5.0), ("lookSpeed", "f", 0.1)],
    "MotionBlurMovementComponent": [("bIsHorizontal", "b", False)],
    "RenderFlagsComponent": [("visible", "b", True), ("bake", "b", True), ("ddgi", "b", True), ("motionBlur", "b", True),
                             ("alphaCutout", "b", True), ("emissiveLight", "b", False), ("cameraMotionBlur", "b", True)],
    "CheckpointComponent": [("checkpointId", "u", 0), ("priority", "i", 0), ("spawnOffset", "v3", [0.0, 0.0, 0.0]),
                            ("spawnRotation", "v3", [0.0, 0.0, 0.0])],
    "PlayerSpawnComponent": [("priority", "i", 0), ("offset", "v3", [0.0, 0.0, 0.0])],
    "RotateInPlaceComponent": [("axis", "v3", [0.0, 1.0, 0.0]), ("speedDegrees", "f", 45.0), ("bWorldSpace", "b", False)],
    "PathMoverComponent": [("spline", S(SPLINE), {}), ("pointSettings", ("list", S(PATH_POINT_SETTINGS)), []), ("loopMode", "u", 2),
                           ("currentSegment", "i", 0), ("progress", "f", 0.0), ("direction", "i", 1), ("bIsWaiting", "b", False),
                           ("waitTimer", "f", 0.0)],
    "DebugGizmoComponent": [("shape", "u", 1), ("extents", "v3", [0.5, 0.5, 0.5]), ("color", "v4", [0.0, 1.0, 0.0, 1.0]), ("lineWidth", "f", 0.05)],
    "StaticMeshComponent": [("modelId", "u", 0), ("shadingShaderOverride", "u", 0), ("lightingShaderOverride", "u", 0)] + RENDER_TAIL,
    "StaticMeshOverridesComponent": [("materialOverrides", ("list", S([("slot", "u", 0), ("id", "u", 0)])), []),
                                     ("primitiveBlacklist", ("list", "u"), [])],
    "StaticMeshPrimitiveComponent": [("modelId", "u", 0), ("primitiveOrdinal", "u", 0xFFFFFFFF), ("materialOverride", "u", 0),
                                     ("shadingShaderOverride", "u", 0), ("lightingShaderOverride", "u", 0)] + RENDER_TAIL,
    "ModuleMeshComponent": [("params", S(MODULE_PARAMS), {}), ("slotMaterials", ("array", "u", 8), [])] + RENDER_TAIL,
    "ProceduralMeshComponent": [("type", PROC, NO_PROC), ("material", "u", 0)] + RENDER_TAIL,
    "SplineMeshComponent": SPLINE_PARAMS + [("material", "u", 0), ("renderOffset", "v3", [0.0, 0.0, 0.0])],
    "Text3DComponent": [f for f in TEXT3D_SOURCE if f[0] != "precise"] + [("material", "u", 0)] + RENDER_TAIL,
    "TextComponent": [("fontId", "u", 0), ("textMaterialId", "u", 0), ("text", "s", ""), ("scale", "f", 1.0), ("color", "v4", [1.0, 1.0, 1.0, 1.0]),
                      ("align", "u", 0), ("anchor", "u", 0), ("wrapWidth", "f", 0.0)],
    "LocalDDGIVolumeComponent": [("volumeId", "u", 0), ("bEnabled", "b", True), ("probeSpacing", "f", 0.5)],
    "ReflectionProbeComponent": [("probeId", "u", 0), ("bEnabled", "b", True), ("shape", "u", 0), ("fadeMargin", "f", 0.5),
                                 ("captureOffset", "v3", [0.0, 0.0, 0.0]), ("bParallax", "b", True), ("resolution", "u", 1),
                                 ("standInEnvMap", "u", 0), ("standInIntensity", "f", 65536.0)],
    "AreaLightComponent": [("color", "v3", [1.0, 1.0, 1.0]), ("intensity", "f", 65536.0), ("halfWidth", "f", 1.0), ("halfHeight", "f", 1.0),
                           ("range", "f", 10.0), ("coneOuterDegrees", "f", 90.0), ("coneInnerDegrees", "f", 90.0), ("bDisk", "b", False),
                           ("drawEmissiveSurface", "b", True), ("bExcludeFromProbeBake", "b", False)],
    "SphereLightComponent": [("color", "v3", [1.0, 1.0, 1.0]), ("intensity", "f", 65536.0), ("radius", "f", 0.5), ("range", "f", 10.0),
                             ("drawEmissiveSurface", "b", True), ("bExcludeFromProbeBake", "b", False)],
    "DirectionalLightComponent": [("color", "v3", [1.0, 1.0, 1.0]), ("intensity", "f", 131072.0), ("priority", "i", 0),
                                  ("angularRadiusDegrees", "f", 1.0)],
    "SkyboxComponent": [("envMap", "u", 0), ("intensity", "f", 65536.0), ("priority", "i", 0), ("bEnabled", "b", True)],
    "PhysicsBodyDesc": [("motionType", "u", 0), ("mass", "f", 1.0), ("friction", "f", 0.5), ("restitution", "f", 0.0), ("motionQuality", "u", 0),
                        ("layerOverride", "u", 0xFFFF), ("enhancedInternalEdgeRemoval", "b", False), ("isSensor", "b", False),
                        ("shapes", ("list", S(PHYSICS_SHAPE)), [])],
}

TAGS = ["DeathZoneComponent", "AntiGravityTag", "FloorTag", "DrawPhysicsDebugTag"]


# ---- adapters: wscene_authoring dict shapes -> reflected field dicts ----

def proc_value(ptype, j):
    ptype = int(ptype)
    if ptype == 0:
        return NO_PROC
    fields = dict(j)
    if ptype == 25:
        openings = [o if isinstance(o, dict) else dict(zip("xywh", o)) for o in j.get("openings", [])]
        fields["openings"] = openings
        fields.setdefault("openingCount", len(openings))
    return (ptype, fields)


def spline_value(j):
    points = j.get("points", [])
    rolls = j.get("rolls", [])
    return {"mode": j.get("mode", 1), "bClosed": j.get("bClosed", False),
            "points": [p if isinstance(p, dict) else {"pos": p, "roll": rolls[i] if i < len(rolls) else 0.0} for i, p in enumerate(points)]}


def spline_params_value(j):
    """Flat spline_fields() dict -> SplineParams (profile/railing blocks)."""
    v = {k: j[k] for k in ("radius", "rollAngle", "sides", "segmentsPerSpan", "bCaps", "bCrossPlanks", "crossPlankInterval",
                           "crossPlankHeight", "crossPlankThickness", "crossPlankLength") if k in j}
    v["spline"] = spline_value(j.get("spline", {}))
    v["profile"] = {"type": j.get("profileType", 0), "width": j.get("profileWidth", 0.4), "height": j.get("profileHeight", 0.4),
                    "cornerRadius": j.get("profileCornerRadius", 0.08), "cornerSegments": j.get("profileCornerSegments", 3),
                    "thickness": j.get("profileThickness", 0.05)}
    v["railing"] = {"bEnabled": j.get("railingEnabled", False), "lanes": j.get("railingLanes", []), "bPosts": j.get("railingPosts", True),
                    "postInterval": j.get("railingPostInterval", 4), "postBottom": j.get("railingPostBottom", 0.0),
                    "postTop": j.get("railingPostTop", 1.0), "postSize": [j.get("railingPostSizeX", 0.05), j.get("railingPostSizeY", 0.05)],
                    "postLateral": j.get("railingPostLateral", 0.0), "lateralOffset": j.get("railingLateralOffset", 0.0)}
    return v


def a_path_mover(j):
    v = dict(j)
    v["spline"] = spline_value(j.get("spline", {}))
    settings = []
    for ps in j.get("pointSettings", []):
        r = ps.get("rotation", [0.0, 0.0, 0.0, 1.0])  # authored x,y,z,w
        settings.append({**ps, "rotation": [r[3], r[0], r[1], r[2]]})
    v["pointSettings"] = settings
    return v


def a_module_mesh(j):
    v = dict(j)
    v["params"] = {"parts": [{**p, "type": proc_value(p.get("type", 0), p)} for p in j.get("parts", [])]}
    v["slotMaterials"] = list(j.get("slotMaterials", []))
    return v


def a_procedural_mesh(j):
    return {**j, "type": proc_value(j.get("type", 0), j)}


def a_spline_mesh(j):
    return {**spline_params_value(j), "material": j.get("material", 0), "renderOffset": j.get("renderOffset", [0.0, 0.0, 0.0])}


def a_static_mesh_overrides(j):
    return {"materialOverrides": [{"slot": int(slot), "id": mid} for slot, mid in j.get("materialOverrides", {}).items()],
            "primitiveBlacklist": list(j.get("primitiveBlacklist", []))}


def a_physics_shape(s):
    stype = min(int(s.get("type", 0)), 3)  # legacy 4/5 were Collider
    fields = dict(s)
    if stype == 3:
        fields["proceduralType"] = proc_value(s.get("proceduralType", 0), s)
        if "splineParams" in s:
            fields["splineParams"] = spline_params_value(s["splineParams"])
    v = {"type": (stype, fields), "offset": s.get("offset", [0.0, 0.0, 0.0]), "rotation": s.get("rotation", [1.0, 0.0, 0.0, 0.0])}
    v["bakedScale"] = s.get("bakedScale", [s.get("bakedScaleX", 1.0), s.get("bakedScaleY", 1.0), s.get("bakedScaleZ", 1.0)])
    return v


def a_physics_body(j):
    return {**j, "shapes": [a_physics_shape(s) for s in j.get("shapes", [])]}


ADAPTERS = {
    "PathMoverComponent": a_path_mover,
    "ModuleMeshComponent": a_module_mesh,
    "ProceduralMeshComponent": a_procedural_mesh,
    "SplineMeshComponent": a_spline_mesh,
    "StaticMeshOverridesComponent": a_static_mesh_overrides,
    "PhysicsBodyDesc": a_physics_body,
}

COMPONENTS = {type_key(n): n for n in FIELDS}
TAG_KEYS = {type_key(n) for n in TAGS}


def emit_component_block(w, type_id, comp):
    w.begin(type_id)
    if type_id in COMPONENTS:
        name = COMPONENTS[type_id]
        v = comp or {}
        if name in ADAPTERS:
            v = ADAPTERS[name](v)
        write_fields(w, FIELDS[name], v)
    elif type_id not in TAG_KEYS:
        raise ValueError("unknown component typeId " + type_id)
    w.end()


def scene_body(j):
    w = W()
    w.key("scene_id", int(j["scene_id"]))
    entities = j.get("entities", [])
    if entities:
        w.key("entities", len(entities))
        for e in entities:
            w.begin("entity")
            for type_id, comp in e.items():
                emit_component_block(w, type_id, comp)
            w.end()
    if "editor_camera" in j:
        cam = j["editor_camera"]
        w.begin("editor_camera")
        w.key_f("translation", *cam.get("translation", [0.0, 0.0, 0.0]))
        w.key_f("rotation", *cam.get("rotation", [1.0, 0.0, 0.0, 0.0]))
        w.end()
    return w.text()


def prefab_body(j):
    w = W()
    for type_id, comp in j.items():
        emit_component_block(w, type_id, comp)
    return w.text()
