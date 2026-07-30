#!/usr/bin/env python3
"""
Reads the id/name of every asset under assets/ so generators can name art instead of
pasting ids. Read-only; importing or running it never writes.

    import asset_index
    idx = asset_index.scan()
    idx.model("sponza")          -> 14462843906823022947
    idx.material("black")        -> 18064661787751907525
    idx.font("Roboto")           -> 11302268835193496650

    python scripts/asset_index.py            # print everything
    python scripts/asset_index.py material   # print one kind
    python scripts/asset_index.py --dupes    # only names that resolve ambiguously

Every asset format is a text header (magic line, then "key value" lines, then
end_header) carrying both `id` and `name`, so one parser covers all nine.

Names are NOT guaranteed unique on disk: two assets may share a name and differ by id,
which is the failure that produced the 2026-07-27 material collision. A lookup that
matches more than one asset raises rather than picking one.
"""
import difflib
import os
import sys

# Header magic -> kind. The magic does not always match the extension (.wsmesh is
# "wstaticmodel", .wtextmaterial is "wstextmat"), so dispatch on the magic.
MAGIC_TO_KIND = {
    "wstaticmodel": "model",
    "wtexture": "texture",
    "wmaterial": "material",
    "wstextmat": "text_material",
    "wprefab": "prefab",
    "wscene": "scene",
    "wprobe": "probe",
    "wsfont": "font",
    "wenvmap": "envmap",
}

ASSET_EXTENSIONS = {
    ".wsmesh", ".wtexture", ".wmaterial", ".wtextmaterial",
    ".wprefab", ".wscene", ".wprobe", ".wsfont", ".wenvmap",
}

HEADER_READ_BYTES = 4096


class AssetLookupError(LookupError):
    """Not KeyError: KeyError reprs its argument, which escapes the newlines in the ambiguity report."""


class Asset:
    def __init__(self, kind, name, asset_id, path, fields):
        self.kind = kind
        self.name = name
        self.asset_id = asset_id
        self.path = path
        self.fields = fields

    @property
    def stem(self):
        return os.path.splitext(os.path.basename(self.path))[0]

    def __repr__(self):
        return "Asset({}, {!r}, {})".format(self.kind, self.name, self.asset_id)


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_header(path):
    """Parses one asset header. Returns (magic, {key: value}) or None if the file is not an asset."""
    with open(path, "rb") as f:
        raw = f.read(HEADER_READ_BYTES)
    text = raw.decode("utf-8", errors="replace")
    lines = text.split("\n")
    magic = lines[0].strip().strip("\r")
    if magic not in MAGIC_TO_KIND:
        return None
    fields = {}
    for line in lines[1:]:
        line = line.strip("\r").rstrip("\n")
        if line == "end_header":
            break
        if not line:
            continue
        key, _, value = line.partition(" ")
        if key:
            fields[key] = value.strip()
    return magic, fields


def _aliases(asset):
    """Every spelling a caller might reasonably use. Model names carry the source extension
    ("sponza.gltf"), so the bare stem has to resolve too."""
    out = {asset.name, asset.stem}
    base, ext = os.path.splitext(asset.name)
    if ext:
        out.add(base)
    return {a.lower() for a in out if a}


class AssetIndex:
    def __init__(self, root):
        self.root = root
        self.assets = []
        self._by_kind = {}

    def _add(self, asset):
        self.assets.append(asset)
        table = self._by_kind.setdefault(asset.kind, {})
        for alias in _aliases(asset):
            table.setdefault(alias, []).append(asset)

    def all(self, kind):
        return [a for a in self.assets if a.kind == kind]

    def find(self, kind, name):
        """Resolves to a single Asset. Raises on miss or ambiguity; never guesses."""
        table = self._by_kind.get(kind, {})
        matches = table.get(name.lower(), [])
        if len(matches) == 1:
            return matches[0]
        if not matches:
            known = sorted({a.name for a in self.all(kind)})
            near = difflib.get_close_matches(name, known, n=5, cutoff=0.4)
            hint = "\n  did you mean: " + ", ".join(near) if near else ""
            raise AssetLookupError("no {} named {!r} under {}{}".format(kind, name, self.root, hint))
        where = "\n  ".join("{} (id {})".format(os.path.relpath(m.path, self.root), m.asset_id) for m in matches)
        raise AssetLookupError("{} name {!r} is ambiguous, {} assets share it:\n  {}".format(kind, name, len(matches), where))

    def lookup(self, kind, name):
        return self.find(kind, name).asset_id

    def duplicates(self):
        """(kind, name, [assets]) for every name that cannot be resolved unambiguously."""
        out = []
        for kind, table in sorted(self._by_kind.items()):
            for alias, assets in sorted(table.items()):
                distinct = {a.asset_id for a in assets}
                if len(distinct) > 1:
                    out.append((kind, alias, assets))
        return out

    def model(self, name): return self.lookup("model", name)
    def texture(self, name): return self.lookup("texture", name)
    def material(self, name): return self.lookup("material", name)
    def text_material(self, name): return self.lookup("text_material", name)
    def prefab(self, name): return self.lookup("prefab", name)
    def scene(self, name): return self.lookup("scene", name)
    def probe(self, name): return self.lookup("probe", name)
    def font(self, name): return self.lookup("font", name)
    def envmap(self, name): return self.lookup("envmap", name)


def scan(root=None):
    root = root or os.path.join(_repo_root(), "assets")
    index = AssetIndex(root)
    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if os.path.splitext(filename)[1] not in ASSET_EXTENSIONS:
                continue
            path = os.path.join(dirpath, filename)
            parsed = read_header(path)
            if parsed is None:
                continue
            magic, fields = parsed
            if "id" not in fields:
                continue
            index._add(Asset(MAGIC_TO_KIND[magic], fields.get("name", ""), int(fields["id"]), path, fields))
    return index


def main():
    args = [a for a in sys.argv[1:]]
    bDupesOnly = "--dupes" in args
    kinds = [a for a in args if not a.startswith("--")]

    index = scan()

    if bDupesOnly:
        dupes = index.duplicates()
        for kind, name, assets in dupes:
            print("{} {!r}:".format(kind, name))
            for a in assets:
                print("    {:<22} {}".format(a.asset_id, os.path.relpath(a.path, index.root)))
        print("{} ambiguous name(s)".format(len(dupes)))
        return

    for kind in kinds or sorted(MAGIC_TO_KIND.values()):
        assets = sorted(index.all(kind), key=lambda a: a.name.lower())
        if not assets:
            continue
        print("=== {} ({}) ===".format(kind, len(assets)))
        for a in assets:
            print("  {:<22} {:<44} {}".format(a.asset_id, a.name, os.path.relpath(a.path, index.root)))


if __name__ == "__main__":
    main()
