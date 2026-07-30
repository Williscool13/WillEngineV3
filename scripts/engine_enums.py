#!/usr/bin/env python3
"""
Reads enum orderings and the ProceduralParams variant order straight out of the C++ headers,
so generators write the integer the engine will actually read. Read-only.

    import engine_enums
    e = engine_enums.scan()
    e.value("PhysicsShapeType", "Capsule")            -> 2
    e.value("ReflectionProbeComponent::Resolution", "Res256") -> 1
    e.procedural("BoxParams")                         -> 2

    python scripts/engine_enums.py                    # print everything
    python scripts/engine_enums.py PhysicsShapeType   # print one

Parsed live rather than exported from a running engine: a dumped table goes stale the
moment someone reorders an enum, and the failure is a silently wrong integer in an asset.

Serialized orderings are load-bearing. Appending is safe; inserting or reordering rewrites
the meaning of every value already on disk (same hazard the component-key migration fixed).
"""
import difflib
import os
import re
import sys

SOURCE_ROOT = "src"

DECL = re.compile(
    r"\b(?:(?P<agg>struct|class)\s+(?P<aggname>[A-Za-z_]\w*)"
    r"|enum\s+class\s+(?P<enumname>[A-Za-z_]\w*))"
    r"(?:\s*:\s*[^{;]+?)?\s*(?P<tail>[{;])"
)

VARIANT = re.compile(r"using\s+(?P<name>\w+)\s*=\s*std::variant\s*<(?P<args>.*?)>\s*;", re.S)

INT_LITERAL = re.compile(r"^[+-]?(?:0[xX][0-9a-fA-F]+|\d+)$")


class EnumLookupError(LookupError):
    """Not KeyError: KeyError reprs its argument, escaping the newlines in the ambiguity report."""


def _repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def strip_comments(text):
    out = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", out)


def _matching_brace(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def _parse_members(body):
    """C++ rule: an unvalued member is previous + 1, starting at 0."""
    members = {}
    nextValue = 0
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        name, _, rawValue = part.partition("=")
        name = name.strip()
        if not re.fullmatch(r"[A-Za-z_]\w*", name):
            continue
        rawValue = rawValue.strip()
        if rawValue:
            if not INT_LITERAL.fullmatch(rawValue):
                # A computed initialiser would desync every following member; refuse rather than guess.
                raise ValueError("non-literal enumerator {} = {!r}".format(name, rawValue))
            nextValue = int(rawValue, 0)
        members[name] = nextValue
        nextValue += 1
    return members


def _split_template_args(text):
    args, depth, current = [], 0, ""
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        args.append(current.strip())
    return args


def parse_file(path):
    """Returns (enums, variants); enum names are qualified by any enclosing struct/class."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = strip_comments(f.read())

    enums = {}
    stack = []
    for match in DECL.finditer(text):
        while stack and stack[-1][1] < match.start():
            stack.pop()
        if match.group("tail") == ";":
            continue
        openIndex = text.index("{", match.end() - 1)
        closeIndex = _matching_brace(text, openIndex)
        if closeIndex < 0:
            continue
        if match.group("enumname"):
            scope = "::".join(name for name, _ in stack)
            qualified = "{}::{}".format(scope, match.group("enumname")) if scope else match.group("enumname")
            try:
                enums[qualified] = _parse_members(text[openIndex + 1:closeIndex])
            except ValueError:
                continue
        else:
            stack.append((match.group("aggname"), closeIndex))

    variants = {}
    for match in VARIANT.finditer(text):
        alternatives = _split_template_args(match.group("args"))
        variants[match.group("name")] = {alt: i for i, alt in enumerate(alternatives)}
    return enums, variants


class EnumIndex:
    def __init__(self, root):
        self.root = root
        self.enums = {}
        self.variants = {}
        self.sources = {}

    def _add_enum(self, qualified, members, path):
        self.enums.setdefault(qualified, []).append((members, path))

    def _resolve(self, name):
        entries = self.enums.get(name)
        if entries is None:
            short = {q.rsplit("::", 1)[-1]: q for q in self.enums}
            if name in short:
                return self._resolve(short[name])
            near = difflib.get_close_matches(name, sorted(self.enums) + sorted(short), n=5, cutoff=0.4)
            hint = "\n  did you mean: " + ", ".join(near) if near else ""
            raise EnumLookupError("no enum named {!r} under {}{}".format(name, self.root, hint))
        distinct = {tuple(sorted(m.items())) for m, _ in entries}
        if len(distinct) > 1:
            where = "\n  ".join(os.path.relpath(p, self.root) for _, p in entries)
            raise EnumLookupError("enum {!r} is declared {} times with differing members:\n  {}".format(name, len(entries), where))
        return entries[0][0]

    def members(self, name):
        return dict(self._resolve(name))

    def value(self, name, member):
        members = self._resolve(name)
        if member not in members:
            near = difflib.get_close_matches(member, sorted(members), n=4, cutoff=0.4)
            hint = "\n  did you mean: " + ", ".join(near) if near else "\n  members: " + ", ".join(sorted(members))
            raise EnumLookupError("enum {} has no member {!r}{}".format(name, member, hint))
        return members[member]

    def procedural(self, alternative, variant="ProceduralParams"):
        alternatives = self.variants.get(variant)
        if alternatives is None:
            raise EnumLookupError("no std::variant named {!r} under {}".format(variant, self.root))
        if alternative not in alternatives:
            near = difflib.get_close_matches(alternative, sorted(alternatives), n=4, cutoff=0.4)
            hint = "\n  did you mean: " + ", ".join(near) if near else ""
            raise EnumLookupError("{} has no alternative {!r}{}".format(variant, alternative, hint))
        return alternatives[alternative]


def scan(root=None):
    root = root or os.path.join(_repo_root(), SOURCE_ROOT)
    index = EnumIndex(root)
    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if not filename.endswith((".h", ".hpp")):
                continue
            path = os.path.join(dirpath, filename)
            enums, variants = parse_file(path)
            for qualified, members in enums.items():
                index._add_enum(qualified, members, path)
            for name, alternatives in variants.items():
                index.variants[name] = alternatives
                index.sources[name] = path
    return index


def main():
    wanted = sys.argv[1:]
    index = scan()

    if wanted:
        for name in wanted:
            print("=== {} ===".format(name))
            for member, value in sorted(index.members(name).items(), key=lambda kv: kv[1]):
                print("  {:<3} {}".format(value, member))
        return

    for name in sorted(index.enums):
        members = index.enums[name][0][0]
        if not members:
            continue
        print("{:<52} {}".format(name, ", ".join("{}={}".format(k, v) for k, v in sorted(members.items(), key=lambda kv: kv[1]))))
    for name, alternatives in sorted(index.variants.items()):
        print()
        print("=== std::variant {} ({}) ===".format(name, len(alternatives)))
        for alt, i in sorted(alternatives.items(), key=lambda kv: kv[1]):
            print("  {:<3} {}".format(i, alt))


if __name__ == "__main__":
    main()
