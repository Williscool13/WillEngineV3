"""Canonical v2 text-body emitters for .wscene/.wprefab (docs/serialization/text_format.md).

Mirrors the C++ TextWriter output byte-for-byte (field order, omit-default,
0x%08x hex floats) so prefab-diff fragment compares stay clean. Scene bodies are
built from the same dict shapes wscene_authoring.py has always produced.
"""

import struct


def fnv1a64(s):
    h = 0xCBF29CE484222325
    for c in s.encode():
        h ^= c
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return str(h)


def hx(v):
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", float(v)))[0]


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


# ---- KeyOpt mirrors (float compares match C++ `!(v == def)`) ----

def opt_f(w, k, j, key, d):
    v = float(j.get(key, d))
    if v != float(d):
        w.key_f(k, v)


def opt_i(w, k, j, key, d):
    v = int(j.get(key, d))
    if v != d:
        w.key(k, v)


def opt_b(w, k, j, key, d):
    v = bool(j.get(key, d))
    if v != d:
        w.key(k, bstr(v))


def opt_vec(w, k, j, key, d):
    v = [float(x) for x in j.get(key, d)]
    if any(a != float(b) for a, b in zip(v, d)):
        w.key_f(k, *v)


def opt_quat_wxyz(w, k, j, key):
    v = [float(x) for x in j.get(key, [1.0, 0.0, 0.0, 0.0])]
    if v != [1.0, 0.0, 0.0, 0.0]:
        w.key_f(k, *v)


def opt_str(w, k, j, key):
    s = j.get(key, "")
    if s:
        w.key_str(k, s)


# ---- shared: spline block body (Engine::Spline::Serialize) ----

def spline_body(w, j):
    opt_i(w, "mode", j, "mode", 1)
    opt_b(w, "bClosed", j, "bClosed", False)
    points = j.get("points", [])
    rolls = j.get("rolls", [])
    if points:
        w.key("points", len(points))
        for i, p in enumerate(points):
            w.begin("p")
            w.key_f("pos", *p)
            roll = float(rolls[i]) if i < len(rolls) else 0.0
            if roll != 0.0:
                w.key_f("roll", roll)
            w.end()


# ---- shared: procedural shape fields (Component::SerializeProceduralShape) ----
# (key, kind, default); kind: f float, i int, b bool. Defaults mirror the C++ deserializer.

def _stair_defaults(j):
    steps = max(int(j.get("stepCount", 0)), 1)
    return float(j.get("totalHeight", 0.0)) / steps


PROC_FIELDS = {
    1: lambda j: [("stepCount", "i", 0), ("width", "f", 0.0), ("totalDepth", "f", 0.0), ("totalHeight", "f", 0.0),
                  ("bSpecifyStepHeight", "b", False), ("stepHeight", "f", _stair_defaults(j)), ("bIsClosed", "b", True)],
    2: lambda j: [("sizeX", "f", 0.0), ("sizeY", "f", 0.0), ("sizeZ", "f", 0.0)],
    3: lambda j: [("radius", "f", 0.0), ("height", "f", 0.0), ("slices", "i", 0), ("bCapped", "b", False)],
    4: lambda j: [("radius", "f", 0.0), ("height", "f", 0.0), ("slices", "i", 0), ("rings", "i", 0)],
    5: lambda j: [("ringRadius", "f", 0.0), ("tubeRadius", "f", 0.0), ("slices", "i", 0), ("stacks", "i", 0)],
    6: lambda j: [("width", "f", 0.0), ("height", "f", 0.0), ("depth", "f", 0.0), ("thickness", "f", 0.0),
                  ("sides", "i", 0), ("bFillCorners", "b", False)],
    7: lambda j: [("sizeX", "f", 0.0), ("sizeY", "f", 0.0), ("sizeZ", "f", 0.0)],
    8: lambda j: [("radius", "f", 0.0), ("height", "f", 0.0), ("slices", "i", 0), ("bCapped", "b", False)],
    9: lambda j: [("width", "f", 0.0), ("height", "f", 0.0), ("depth", "f", 0.0), ("archHeight", "f", 0.5),
                  ("gap", "f", 0.0), ("sides", "i", 0), ("bHalf", "b", False), ("bFlip", "b", False)],
    10: lambda j: [("sizeX", "f", 0.0), ("sizeZ", "f", 0.0), ("tilesX", "i", 0), ("tilesZ", "i", 0)],
    11: lambda j: [("radius", "f", 0.0), ("slices", "i", 0), ("stacks", "i", 0)],
    12: lambda j: [("radius", "f", 0.0), ("subdivisions", "i", 0)],
    13: lambda j: [("radius", "f", 0.0), ("slices", "i", 0), ("stacks", "i", 0)],
    14: lambda j: [("outerRadius", "f", 0.0), ("innerRadius", "f", 0.0), ("height", "f", 0.0), ("slices", "i", 0)],
    15: lambda j: [("radius", "f", 0.0)],
    16: lambda j: [("radius", "f", 0.0)],
    17: lambda j: [("radius", "f", 0.0)],
    18: lambda j: [("radius", "f", 0.0)],
    19: lambda j: [("scale", "f", 0.0), ("slices", "i", 0), ("stacks", "i", 0)],
    20: lambda j: [("scale", "f", 0.0), ("tubeRadius", "f", 0.0), ("slices", "i", 0), ("stacks", "i", 0)],
    21: lambda j: [("width", "f", 0.0), ("height", "f", 0.0), ("radius", "f", 0.0), ("segments", "i", 0),
                   ("bHalfPipe", "b", False), ("flatLength", "f", 1.0), ("lipHeight", "f", 0.02)],
    22: lambda j: [("radius", "f", 0.0), ("height", "f", 0.0), ("curveRadius", "f", 0.0), ("flatRadius", "f", 0.0),
                   ("lipHeight", "f", 0.02), ("slices", "i", 0), ("segments", "i", 0)],
    23: lambda j: [("stepCount", "i", 0), ("stepHeight", "f", 0.0),
                   ("totalHeight", "f", float(j.get("stepHeight", 0.0)) * max(int(j.get("stepCount", 0)), 1)),
                   ("bSpecifyStepHeight", "b", False), ("outerRadius", "f", 0.0), ("centerColumnRadius", "f", 0.0),
                   ("treadThickness", "f", 0.08), ("degreesPerStep", "f", 30.0),
                   ("totalSweep", "f", float(j.get("degreesPerStep", 30.0)) * max(int(j.get("stepCount", 0)), 1)),
                   ("bSpecifyDegreesPerStep", "b", False), ("arcSegments", "i", 6), ("bShowCenterColumn", "b", True),
                   ("bRamp", "b", False)],
    24: lambda j: [("outerRadius", "f", 0.0), ("innerRadius", "f", 0.0), ("slices", "i", 0), ("bDoubleSided", "b", True)],
    25: lambda j: [("sizeX", "f", 0.0), ("sizeY", "f", 0.0), ("sizeZ", "f", 0.0)],
    26: lambda j: [("sizeX", "f", 0.0), ("sizeY", "f", 0.0), ("sizeZ", "f", 0.0), ("chordSize", "f", 0.0),
                   ("braceSize", "f", 0.0), ("bayCount", "i", 0), ("pattern", "i", 0)],
    27: lambda j: [("sizeX", "f", 0.0), ("sizeY", "f", 0.0), ("sizeZ", "f", 0.0), ("ribDepth", "f", 0.0),
                   ("ribWidth", "f", 0.0), ("ribCount", "i", 0)],
}


def proc_shape_fields(w, ptype, j):
    if ptype not in PROC_FIELDS:
        return
    for key, kind, d in PROC_FIELDS[ptype](j):
        if kind == "f":
            w.key_f(key, float(j.get(key, d)))
        elif kind == "i":
            v = int(j.get(key, d))
            if ptype == 12 and key == "subdivisions":
                v = max(0, min(v, 4))
            w.key(key, v)
        else:
            w.key(key, bstr(j.get(key, d)))
    if ptype == 25:
        openings = j.get("openings", [])
        if openings:
            w.key("openings", len(openings))
            for o in openings:
                w.begin("o")
                w.key_f("rect", *o)
                w.end()


# ---- shared: splineParams block body (physics collider shape) ----

def spline_params_body(w, sp):
    w.begin("spline")
    spline_body(w, sp.get("spline", {}))
    w.end()
    w.key_f("radius", sp.get("radius", 0.5))
    w.key_f("rollAngle", sp.get("rollAngle", 0.0))
    w.key("sides", int(sp.get("sides", 8)))
    w.key("segmentsPerSpan", int(sp.get("segmentsPerSpan", 8)))
    w.key("bCaps", bstr(sp.get("bCaps", True)))
    w.key("bCrossPlanks", bstr(sp.get("bCrossPlanks", False)))
    w.key("crossPlankInterval", int(sp.get("crossPlankInterval", 4)))
    w.key_f("crossPlankHeight", sp.get("crossPlankHeight", 0.0))
    w.key_f("crossPlankThickness", sp.get("crossPlankThickness", 0.1))
    w.key_f("crossPlankLength", sp.get("crossPlankLength", 0.3))
    w.key("profileType", int(sp.get("profileType", 0)))
    w.key_f("profileWidth", sp.get("profileWidth", 0.4))
    w.key_f("profileHeight", sp.get("profileHeight", 0.4))
    w.key_f("profileCornerRadius", sp.get("profileCornerRadius", 0.08))
    w.key("profileCornerSegments", int(sp.get("profileCornerSegments", 3)))
    w.key_f("profileThickness", sp.get("profileThickness", 0.05))
    w.key("railingEnabled", bstr(sp.get("railingEnabled", False)))
    w.key("railingPosts", bstr(sp.get("railingPosts", True)))
    w.key("railingPostInterval", int(sp.get("railingPostInterval", 4)))
    w.key_f("railingPostBottom", sp.get("railingPostBottom", 0.0))
    w.key_f("railingPostTop", sp.get("railingPostTop", 1.0))
    w.key_f("railingPostSize", sp.get("railingPostSizeX", 0.05), sp.get("railingPostSizeY", 0.05))
    w.key_f("railingPostLateral", sp.get("railingPostLateral", 0.0))
    w.key_f("railingLateralOffset", sp.get("railingLateralOffset", 0.0))
    lanes = sp.get("railingLanes", [])
    if lanes:
        w.key("railingLanes", len(lanes))
        for lane in lanes:
            w.begin("l")
            w.key_f("lane", *lane)
            w.end()


def text3d_source_body(w, t3):
    w.key("fontId", int(t3.get("fontId", 0)))
    w.key_str("text", t3.get("text", ""))
    w.key_f("depth", t3.get("depth", 0.2))
    w.key_f("flatness", t3.get("flatness", 0.005))
    w.key_f("tracking", t3.get("tracking", 0.0))
    w.key_f("scale", t3.get("scale", 1.0))
    w.key_f("wrapWidth", t3.get("wrapWidth", 0.0))
    w.key_f("bendRadius", t3.get("bendRadius", 0.0))
    w.key("smoothNormals", bstr(t3.get("smoothNormals", True)))
    w.key("align", int(t3.get("align", 0)))
    w.key("anchor", int(t3.get("anchor", 0)))
    w.key("precise", bstr(t3.get("precise", False)))


# ---- per-component emitters ----

def c_transform(w, j):
    opt_vec(w, "translation", j, "translation", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "rotation", j, "rotation")
    opt_vec(w, "scale", j, "scale", [1.0, 1.0, 1.0])


def c_hierarchy(w, j):
    opt_i(w, "parentStableId", j, "parentStableId", 0)


def c_name(w, j):
    opt_str(w, "name", j, "name")


def c_prefab_instance(w, j):
    opt_i(w, "prefabId", j, "prefabId", 0)
    opt_b(w, "bMasterPrefab", j, "bMasterPrefab", False)


def c_stable_id(w, j):
    opt_i(w, "id", j, "id", 0)
    opt_i(w, "sortOrder", j, "sortOrder", 0)


def c_entity_folder(w, j):
    opt_i(w, "folderId", j, "folderId", 0)


def c_scene_folder(w, j):
    opt_i(w, "folderId", j, "folderId", 0)
    opt_i(w, "parentFolder", j, "parentFolder", 0)
    opt_str(w, "name", j, "name")


def c_free_camera(w, j):
    opt_f(w, "moveSpeed", j, "moveSpeed", 5.0)
    opt_f(w, "lookSpeed", j, "lookSpeed", 0.1)


def c_motion_blur(w, j):
    opt_b(w, "bIsHorizontal", j, "bIsHorizontal", False)


def c_render_flags(w, j):
    for k in ("visible", "probeBake", "ddgi", "motionBlur", "alphaCutout"):
        opt_b(w, k, j, k, True)


def c_checkpoint(w, j):
    opt_i(w, "checkpointId", j, "checkpointId", 0)
    opt_i(w, "priority", j, "priority", 0)
    opt_vec(w, "spawnOffset", j, "spawnOffset", [0.0, 0.0, 0.0])
    opt_vec(w, "spawnRotation", j, "spawnRotation", [0.0, 0.0, 0.0])


def c_player_spawn(w, j):
    opt_i(w, "priority", j, "priority", 0)
    opt_vec(w, "offset", j, "offset", [0.0, 0.0, 0.0])


def c_rotate_in_place(w, j):
    opt_vec(w, "axis", j, "axis", [0.0, 1.0, 0.0])
    opt_f(w, "speedDegrees", j, "speedDegrees", 45.0)
    opt_b(w, "bWorldSpace", j, "bWorldSpace", False)


def c_path_mover(w, j):
    w.key("loopMode", int(j.get("loopMode", 0)))
    w.begin("spline")
    spline_body(w, j.get("spline", {}))
    w.end()
    settings = j.get("pointSettings", [])
    if settings:
        w.key("pointSettings", len(settings))
        for ps in settings:
            w.begin("p")
            r = ps.get("rotation", [0.0, 0.0, 0.0, 1.0])  # JSON stored x,y,z,w
            q = [float(r[3]), float(r[0]), float(r[1]), float(r[2])]
            if q != [1.0, 0.0, 0.0, 0.0]:
                w.key_f("rotation", *q)
            opt_i(w, "easing", ps, "easing", 0)
            opt_f(w, "speed", ps, "speed", 1.0)
            opt_f(w, "waitTime", ps, "waitTime", 0.0)
            w.end()
    opt_i(w, "currentSegment", j, "currentSegment", 0)
    opt_f(w, "progress", j, "progress", 0.0)
    opt_i(w, "direction", j, "direction", 1)
    opt_b(w, "bIsWaiting", j, "bIsWaiting", False)
    opt_f(w, "waitTimer", j, "waitTimer", 0.0)


def c_debug_gizmo(w, j):
    opt_i(w, "shape", j, "shape", 1)
    opt_vec(w, "extents", j, "extents", [0.5, 0.5, 0.5])
    opt_vec(w, "color", j, "color", [0.0, 1.0, 0.0, 1.0])
    opt_f(w, "lineWidth", j, "lineWidth", 0.05)


def c_static_mesh(w, j):
    w.key("modelId", int(j.get("modelId", 0)))
    overrides = j.get("materialOverrides", {})
    if overrides:
        w.key("materialOverrides", len(overrides))
        for slot, mid in overrides.items():
            w.begin("m")
            w.key("slot", int(slot))
            w.key("id", int(mid))
            w.end()
    blacklist = j.get("primitiveBlacklist", [])
    if blacklist:
        w.key("primitiveBlacklist", *[int(v) for v in blacklist])
    opt_i(w, "shadingShaderOverride", j, "shadingShaderOverride", 0)
    opt_i(w, "lightingShaderOverride", j, "lightingShaderOverride", 0)
    opt_vec(w, "renderOffset", j, "renderOffset", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "renderRotation", j, "renderRotation")


def c_static_mesh_primitive(w, j):
    w.key("modelId", int(j.get("modelId", 0)))
    w.key("primitiveOrdinal", int(j.get("primitiveOrdinal", 0)) & 0xFFFFFFFF)
    opt_i(w, "materialOverride", j, "materialOverride", 0)
    opt_i(w, "shadingShaderOverride", j, "shadingShaderOverride", 0)
    opt_i(w, "lightingShaderOverride", j, "lightingShaderOverride", 0)
    opt_vec(w, "renderOffset", j, "renderOffset", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "renderRotation", j, "renderRotation")


def c_module_mesh(w, j):
    opt_vec(w, "renderOffset", j, "renderOffset", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "renderRotation", j, "renderRotation")
    slots = j.get("slotMaterials", [])
    w.key("slotMaterials", 8)
    for i in range(8):
        w.begin("s")
        mid = int(slots[i]) if i < len(slots) else 0
        if mid != 0:
            w.key("id", mid)
        w.end()
    parts = j.get("parts", [])
    if parts:
        w.key("parts", len(parts))
        for p in parts:
            w.begin("p")
            ptype = int(p.get("type", 0))
            w.key("type", ptype)
            proc_shape_fields(w, ptype, p)
            w.key_f("offset", *p.get("offset", [0.0, 0.0, 0.0]))
            w.key_f("rotation", *p.get("rotation", [1.0, 0.0, 0.0, 0.0]))  # already w,x,y,z
            w.key("slot", int(p.get("slot", 0)))
            w.end()


def c_procedural_mesh(w, j):
    ptype = int(j.get("type", 0))
    w.key("type", ptype)
    w.key("material", int(j.get("material", 0)))
    opt_vec(w, "renderOffset", j, "renderOffset", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "renderRotation", j, "renderRotation")
    proc_shape_fields(w, ptype, j)


def c_spline_mesh(w, j):
    w.begin("spline")
    spline_body(w, j.get("spline", {}))
    w.end()
    spm = dict(j)
    spm.pop("spline", None)
    # same always-write field run as splineParams minus the wrapper block, plus material
    w.key_f("radius", j.get("radius", 0.5))
    w.key_f("rollAngle", j.get("rollAngle", 0.0))
    w.key("sides", int(j.get("sides", 8)))
    w.key("segmentsPerSpan", int(j.get("segmentsPerSpan", 8)))
    w.key("bCaps", bstr(j.get("bCaps", True)))
    w.key("bCrossPlanks", bstr(j.get("bCrossPlanks", False)))
    w.key("crossPlankInterval", int(j.get("crossPlankInterval", 4)))
    w.key_f("crossPlankHeight", j.get("crossPlankHeight", 0.0))
    w.key_f("crossPlankThickness", j.get("crossPlankThickness", 0.1))
    w.key_f("crossPlankLength", j.get("crossPlankLength", 0.3))
    w.key("profileType", int(j.get("profileType", 0)))
    w.key_f("profileWidth", j.get("profileWidth", 0.4))
    w.key_f("profileHeight", j.get("profileHeight", 0.4))
    w.key_f("profileCornerRadius", j.get("profileCornerRadius", 0.08))
    w.key("profileCornerSegments", int(j.get("profileCornerSegments", 3)))
    w.key_f("profileThickness", j.get("profileThickness", 0.05))
    w.key("railingEnabled", bstr(j.get("railingEnabled", False)))
    w.key("railingPosts", bstr(j.get("railingPosts", True)))
    w.key("railingPostInterval", int(j.get("railingPostInterval", 4)))
    w.key_f("railingPostBottom", j.get("railingPostBottom", 0.0))
    w.key_f("railingPostTop", j.get("railingPostTop", 1.0))
    w.key_f("railingPostSize", j.get("railingPostSizeX", 0.05), j.get("railingPostSizeY", 0.05))
    w.key_f("railingPostLateral", j.get("railingPostLateral", 0.0))
    w.key_f("railingLateralOffset", j.get("railingLateralOffset", 0.0))
    lanes = j.get("railingLanes", [])
    if lanes:
        w.key("railingLanes", len(lanes))
        for lane in lanes:
            w.begin("l")
            w.key_f("lane", *lane)
            w.end()
    w.key("material", int(j.get("material", 0)))


def c_text3d(w, j):
    w.key("fontId", int(j.get("fontId", 0)))
    opt_str(w, "text", j, "text")
    opt_f(w, "depth", j, "depth", 0.2)
    opt_f(w, "flatness", j, "flatness", 0.005)
    opt_f(w, "tracking", j, "tracking", 0.0)
    opt_f(w, "scale", j, "scale", 1.0)
    opt_f(w, "wrapWidth", j, "wrapWidth", 0.0)
    opt_f(w, "bendRadius", j, "bendRadius", 0.0)
    opt_b(w, "smoothNormals", j, "smoothNormals", True)
    opt_i(w, "align", j, "align", 0)
    opt_i(w, "anchor", j, "anchor", 0)
    w.key("material", int(j.get("material", 0)))
    opt_vec(w, "renderOffset", j, "renderOffset", [0.0, 0.0, 0.0])
    opt_quat_wxyz(w, "renderRotation", j, "renderRotation")


def c_text(w, j):
    w.key("fontId", int(j.get("fontId", 0)))
    opt_i(w, "textMaterialId", j, "textMaterialId", 0)
    opt_str(w, "text", j, "text")
    opt_f(w, "scale", j, "scale", 1.0)
    opt_vec(w, "color", j, "color", [1.0, 1.0, 1.0, 1.0])
    opt_i(w, "align", j, "align", 0)
    opt_i(w, "anchor", j, "anchor", 0)
    opt_f(w, "wrapWidth", j, "wrapWidth", 0.0)


def c_local_ddgi(w, j):
    opt_i(w, "volumeId", j, "volumeId", 0)
    opt_b(w, "bEnabled", j, "bEnabled", True)
    opt_f(w, "probeSpacing", j, "probeSpacing", 0.5)


def c_reflection_probe(w, j):
    opt_i(w, "probeId", j, "probeId", 0)
    opt_b(w, "bEnabled", j, "bEnabled", True)
    opt_i(w, "shape", j, "shape", 0)
    opt_f(w, "fadeMargin", j, "fadeMargin", 0.5)
    opt_vec(w, "captureOffset", j, "captureOffset", [0.0, 0.0, 0.0])
    opt_b(w, "bParallax", j, "bParallax", True)
    opt_i(w, "resolution", j, "resolution", 1)
    opt_i(w, "standInEnvMap", j, "standInEnvMap", 0)


def c_area_light(w, j):
    opt_vec(w, "color", j, "color", [1.0, 1.0, 1.0])
    opt_f(w, "intensity", j, "intensity", 1.0)
    opt_f(w, "halfWidth", j, "halfWidth", 1.0)
    opt_f(w, "halfHeight", j, "halfHeight", 1.0)
    opt_f(w, "range", j, "range", 10.0)
    opt_b(w, "drawEmissiveSurface", j, "drawEmissiveSurface", True)
    opt_b(w, "bExcludeFromProbeBake", j, "bExcludeFromProbeBake", False)


def c_sphere_light(w, j):
    opt_vec(w, "color", j, "color", [1.0, 1.0, 1.0])
    opt_f(w, "intensity", j, "intensity", 1.0)
    opt_f(w, "radius", j, "radius", 0.5)
    opt_f(w, "range", j, "range", 10.0)
    opt_b(w, "drawEmissiveSurface", j, "drawEmissiveSurface", True)
    opt_b(w, "bExcludeFromProbeBake", j, "bExcludeFromProbeBake", False)


def c_directional_light(w, j):
    opt_vec(w, "color", j, "color", [1.0, 1.0, 1.0])
    opt_f(w, "intensity", j, "intensity", 2.0)
    opt_i(w, "priority", j, "priority", 0)
    opt_f(w, "angularRadiusDegrees", j, "angularRadiusDegrees", 1.0)


def c_skybox(w, j):
    opt_i(w, "envMap", j, "envMap", 0)
    opt_f(w, "intensity", j, "intensity", 1.0)
    opt_i(w, "priority", j, "priority", 0)


def c_physics_body(w, j):
    w.key("motionType", int(j.get("motionType", 0)))
    w.key_f("mass", j.get("mass", 1.0))
    w.key_f("friction", j.get("friction", 0.0))
    w.key_f("restitution", j.get("restitution", 0.0))
    w.key("motionQuality", int(j.get("motionQuality", 0)))
    w.key("layerOverride", int(j.get("layerOverride", 0xFFFF)))
    w.key("enhancedInternalEdgeRemoval", bstr(j.get("enhancedInternalEdgeRemoval", False)))
    w.key("isSensor", bstr(j.get("isSensor", False)))
    shapes = j.get("shapes", [])
    if not shapes:
        return
    w.key("shapes", len(shapes))
    for s in shapes:
        w.begin("shape")
        stype = min(int(s.get("type", 0)), 3)  # legacy 4/5 collapse to Collider, matching the C++ migration
        w.key("type", stype)
        w.key_f("offset", *s.get("offset", [0.0, 0.0, 0.0]))
        w.key_f("rotation", *s.get("rotation", [1.0, 0.0, 0.0, 0.0]))  # already w,x,y,z
        w.key_f("bakedScale", s.get("bakedScaleX", 1.0), s.get("bakedScaleY", 1.0), s.get("bakedScaleZ", 1.0))
        if stype == 0:
            w.key_f("halfExtents", *s.get("halfExtents", [0.5, 0.5, 0.5]))
        elif stype == 1:
            w.key_f("radius", s.get("radius", 0.5))
        elif stype == 2:
            w.key_f("radius", s.get("radius", 0.5))
            w.key_f("halfHeight", s.get("halfHeight", 0.5))
        elif stype == 3:
            w.key("meshSourceModelId", int(s.get("meshSourceModelId", 0)))
            w.key("meshPrecise", bstr(s.get("meshPrecise", False)))
            ptype = int(s.get("proceduralType", 0))
            w.key("proceduralType", ptype)
            if "splineParams" in s:
                w.begin("splineParams")
                spline_params_body(w, s["splineParams"])
                w.end()
            if "text3DSource" in s:
                w.begin("text3DSource")
                text3d_source_body(w, s["text3DSource"])
                w.end()
            proc_shape_fields(w, ptype, s)
        w.end()


COMPONENTS = {
    fnv1a64("TransformComponent"): c_transform,
    fnv1a64("HierarchyComponent"): c_hierarchy,
    fnv1a64("NameComponent"): c_name,
    fnv1a64("PrefabInstanceComponent"): c_prefab_instance,
    fnv1a64("StableIdComponent"): c_stable_id,
    fnv1a64("EntityFolderComponent"): c_entity_folder,
    fnv1a64("SceneFolderComponent"): c_scene_folder,
    fnv1a64("FreeCameraComponent"): c_free_camera,
    fnv1a64("MotionBlurMovementComponent"): c_motion_blur,
    fnv1a64("RenderFlagsComponent"): c_render_flags,
    fnv1a64("CheckpointComponent"): c_checkpoint,
    fnv1a64("PlayerSpawnComponent"): c_player_spawn,
    fnv1a64("RotateInPlaceComponent"): c_rotate_in_place,
    fnv1a64("PathMoverComponent"): c_path_mover,
    fnv1a64("DebugGizmoComponent"): c_debug_gizmo,
    fnv1a64("StaticMeshComponent"): c_static_mesh,
    fnv1a64("StaticMeshPrimitiveComponent"): c_static_mesh_primitive,
    fnv1a64("ModuleMeshComponent"): c_module_mesh,
    fnv1a64("ProceduralMeshComponent"): c_procedural_mesh,
    fnv1a64("SplineMeshComponent"): c_spline_mesh,
    fnv1a64("Text3DComponent"): c_text3d,
    fnv1a64("TextComponent"): c_text,
    fnv1a64("LocalDDGIVolumeComponent"): c_local_ddgi,
    fnv1a64("ReflectionProbeComponent"): c_reflection_probe,
    fnv1a64("AreaLightComponent"): c_area_light,
    fnv1a64("SphereLightComponent"): c_sphere_light,
    fnv1a64("DirectionalLightComponent"): c_directional_light,
    fnv1a64("SkyboxComponent"): c_skybox,
    fnv1a64("PhysicsBodyDesc"): c_physics_body,
}

TAGS = {fnv1a64(n) for n in ["DeathZoneComponent", "AntiGravityTag", "FloorTag", "DrawPhysicsDebugTag"]}


def emit_component_block(w, type_id, comp):
    w.begin(type_id)
    if type_id in COMPONENTS:
        COMPONENTS[type_id](w, comp or {})
    elif type_id in TAGS:
        pass
    else:
        raise ValueError("unknown component typeId " + type_id)
    w.end()


def scene_body(j):
    w = W()
    w.key("scene_id", int(j["scene_id"]))
    w.key_str("scene_name", j.get("scene_name", ""))
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


