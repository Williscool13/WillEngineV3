"""One-time .wmaterial v1 (JSON body) -> v2 (bespoke text body) converter.

Mirrors SerializeMaterial in src/engine/resources/material/material.cpp: same key order,
hex-bit floats, samplers|6 record array with omit-default fields. Delete after the
JSON-removal migration completes.
"""
import json
import struct
import sys
from pathlib import Path

VEC4_KEYS = [
    "colorFactor", "metalRoughFactors",
    "colorUvTransform", "metalRoughUvTransform", "normalUvTransform",
    "emissiveUvTransform", "occlusionUvTransform",
    "emissiveFactor", "alphaProperties", "physicalProperties",
]
IVEC4_KEYS = []
PROP_ORDER = [
    "colorFactor", "metalRoughFactors",
    "colorUvTransform", "metalRoughUvTransform", "normalUvTransform",
    "emissiveUvTransform", "occlusionUvTransform",
    "emissiveFactor", "alphaProperties", "physicalProperties",
]
SAMPLER_DEFAULTS = {
    "magFilter": 1, "minFilter": 1, "mipmapMode": 1,
    "addressModeU": 0, "addressModeV": 0, "addressModeW": 0,
    "mipLodBias": 0.0, "minLod": 0.0, "maxLod": 1000.0,
    "anisotropyEnable": 0, "maxAnisotropy": 1.0,
}
SAMPLER_INT_KEYS = {"magFilter", "minFilter", "mipmapMode", "addressModeU", "addressModeV", "addressModeW", "anisotropyEnable"}


def hex_float(v):
    return "0x%08x" % struct.unpack("<I", struct.pack("<f", float(v)))[0]


def escape(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r")


def convert_body(j):
    lines = []
    lines.append("name|" + escape(j["name"]))
    lines.append("id|%d" % j["id"])
    if "fragmentShader" in j:
        lines.append("fragmentShader|%d" % j["fragmentShader"])
    if "lightingShader" in j:
        lines.append("lightingShader|%d" % j["lightingShader"])
    for key in PROP_ORDER:
        vals = j[key]
        if key in IVEC4_KEYS:
            lines.append(key + "|" + "|".join("%d" % v for v in vals))
        else:
            lines.append(key + "|" + "|".join(hex_float(v) for v in vals))
    lines.append("samplers|6")
    refs = j.get("textureRefs", [0] * 6)
    descs = j.get("samplerDesc", [dict(SAMPLER_DEFAULTS)] * 6)
    for i in range(6):
        lines.append("s")
        if refs[i] != 0:
            lines.append("textureRef|%d" % refs[i])
        for key, default in SAMPLER_DEFAULTS.items():
            v = descs[i][key]
            if key in SAMPLER_INT_KEYS:
                if int(v) != default:
                    lines.append("%s|%d" % (key, int(v)))
            elif hex_float(v) != hex_float(default):
                lines.append("%s|%s" % (key, hex_float(v)))
        lines.append(";")
    return "\n".join(lines) + "\n"


def convert_file(path):
    raw = path.read_bytes().decode("utf-8")
    header_end = raw.index("\n", raw.index("end_header")) + 1
    header = raw[:header_end].replace("\r\n", "\n")
    if "version 2 0" in header:
        return False
    header = header.replace("version 1 0", "version 2 0")
    body = convert_body(json.loads(raw[header_end:]))
    path.write_bytes((header + body).encode("utf-8"))
    return True


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets"
    converted = 0
    for path in sorted(root.rglob("*.wmaterial")):
        if convert_file(path):
            converted += 1
            print("converted", path)
    print("%d files converted" % converted)


if __name__ == "__main__":
    main()
