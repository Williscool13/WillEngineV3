#!/usr/bin/env python3
"""
Rewrites the component-type keys in every .wscene and .wprefab from the MSVC-signature
hash to a hash of the component's registered name.

Keys used to be fnv1a64 of __FUNCSIG__, which on MSVC expands to
    struct StringID __cdecl Game::TypeSID<struct Game::Component::<TypeName>>(void)
making them compiler-specific (clang/gcc take the __PRETTY_FUNCTION__ branch and produce an
entirely different string) and sensitive to renames, namespace moves and struct/class flips.
They are now fnv1a64 of each component's COMPONENT_NAME, which Game::TypeSID hashes directly.

PAIRED WITH THE ENGINE CHANGE that redefined TypeSID. Running the forward pass against a binary
that still hashes signatures, or --revert against one that does not, leaves every entity with
ZERO components; the read sites log a warning per key but still drop them, and saving such a
scene from the editor makes the loss permanent. Keep the two in the same commit.

Usage:
    python scripts/migrate_component_keys.py                  # dry run: mapping + coverage, writes nothing
    python scripts/migrate_component_keys.py --apply
    python scripts/migrate_component_keys.py --apply --revert       # back to TypeSID keys
    python scripts/migrate_component_keys.py --apply --drop-unknown # also drop unregistered keys
"""
import argparse
import glob
import json
import os
import re
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY_CPP = os.path.join(REPO, "src", "game", "component-registry", "component_registry.cpp")
BACKUP_DIR = os.path.join(REPO, ".component_key_backup")   # deliberately NOT under assets/, which the asset manager scans
TARGETS = [("assets/scenes", ".wscene"), ("assets/prefabs", ".wprefab")]

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1

# Verified 2026-07-27 by brute force against six known keys, then validated across every asset
# on disk: 25 of the 26 distinct keys present are explained by it. The one holdout is the
# removed PointLightComponent (see UNKNOWN handling below).
FUNCSIG = "struct StringID __cdecl Game::TypeSID<struct Game::Component::{}>(void)"

def fnv1a64(text):
    h = FNV_OFFSET
    for b in text.encode("utf-8"):
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h

def read_registrations():
    """[(typeName, COMPONENT_NAME)] for every registered component, pairing the registration list
    with each struct's COMPONENT_NAME member."""
    with open(REGISTRY_CPP, "r", encoding="utf-8") as f:
        types = re.findall(r'RegisterComponent<\s*(?:Component::)?([A-Za-z0-9_]+)\s*>', f.read())
    if not types:
        sys.exit(f"no RegisterComponent calls parsed from {REGISTRY_CPP}; the call shape must have changed")

    headers = sorted(set(glob.glob(os.path.join(REPO, "src/game/**/*.h"), recursive=True)))
    names = {}
    for path in headers:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
        for type_name, comp_name in re.findall(r'struct\s+([A-Za-z0-9_]+)\b[^\n{]*\n?\s*\{[^}]{0,200}?COMPONENT_NAME\s*=\s*"([^"]+)"', text, re.S):
            names.setdefault(type_name, comp_name)

    missing = [t for t in types if t not in names]
    if missing:
        sys.exit(f"registered components with no COMPONENT_NAME found in headers: {missing}")
    return [(t, names[t]) for t in types]

def build_maps(regs):
    """(old->new, new->old, key->componentName). Refuses on any collision."""
    forward, reverse, label = {}, {}, {}
    for type_name, reg_name in regs:
        old = str(fnv1a64(FUNCSIG.format(type_name)))
        new = str(fnv1a64(reg_name))
        if old in forward:
            sys.exit(f"old-key collision on {reg_name}")
        if new in reverse:
            sys.exit(f"NAME-key collision: {reg_name} hashes to an already-used key; rename one component")
        forward[old] = new
        reverse[new] = old
        label[old] = reg_name
        label[new] = reg_name
    return forward, reverse, label

def target_files():
    out = []
    for rel, ext in TARGETS:
        directory = os.path.join(REPO, rel)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if name.endswith(ext):
                out.append(os.path.join(directory, name))
    return out

def split_file(path):
    """(headerText, bodyObject). headerText is '' for the headerless legacy scenes."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    marker = text.find("end_header")
    if marker < 0:
        return "", json.loads(text)
    cut = marker + len("end_header")
    return text[:cut], json.loads(text[cut:])

def write_file(path, header, body):
    with open(path, "w", encoding="utf-8") as f:
        if header:
            f.write(header + "\n")
        f.write(json.dumps(body, indent=2))

def component_maps(body):
    """Every dict in the file whose keys are component-type ids.
    .wscene nests them one per entity under 'entities'; .wprefab is one flat map."""
    if isinstance(body, dict) and "entities" in body:
        return [e for e in body["entities"] if isinstance(e, dict)]
    if isinstance(body, dict):
        return [body]
    return []

def classify(maps, forward, reverse):
    old, new, unknown = set(), set(), set()
    for comp_map in maps:
        for key in comp_map:
            if not key.isdigit():
                continue
            if key in forward:
                old.add(key)
            elif key in reverse:
                new.add(key)
            else:
                unknown.add(key)
    return old, new, unknown

def remap(maps, mapping, drop_unknown, known):
    """Rewrites keys in place. Returns (remapped, dropped)."""
    remapped = dropped = 0
    for comp_map in maps:
        for key in list(comp_map):
            if not key.isdigit():
                continue
            if key in mapping:
                comp_map[mapping[key]] = comp_map.pop(key)
                remapped += 1
            elif key not in known and drop_unknown:
                comp_map.pop(key)
                dropped += 1
    return remapped, dropped

def backup(paths, force):
    if os.path.isdir(BACKUP_DIR) and os.listdir(BACKUP_DIR):
        if not force:
            sys.exit(f"{BACKUP_DIR} already exists and is not empty; move it aside or pass --force")
        shutil.rmtree(BACKUP_DIR)
    for path in paths:
        dst = os.path.join(BACKUP_DIR, os.path.relpath(path, REPO))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(path, dst)
    print(f"backed up {len(paths)} files to {os.path.relpath(BACKUP_DIR, REPO)}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true", help="write the files (default is a dry run)")
    ap.add_argument("--revert", action="store_true", help="map name keys back to TypeSID keys")
    ap.add_argument("--drop-unknown", action="store_true", help="delete keys that match no registered component")
    ap.add_argument("--force", action="store_true", help="overwrite an existing backup directory")
    args = ap.parse_args()

    regs = read_registrations()
    forward, reverse, label = build_maps(regs)
    mapping = reverse if args.revert else forward
    known = set(forward) | set(reverse)

    print(f"{len(regs)} registered components, {'name -> TypeSID' if args.revert else 'TypeSID -> name'} keys\n")

    files, plans, blocked = target_files(), [], []
    all_unknown = {}
    for path in files:
        rel = os.path.relpath(path, REPO)
        try:
            header, body = split_file(path)
        except Exception as ex:
            blocked.append((rel, f"unreadable: {ex}"))
            continue
        maps = component_maps(body)
        if not maps:
            blocked.append((rel, "no component maps found"))
            continue
        old, new, unknown = classify(maps, forward, reverse)
        for key in unknown:
            all_unknown.setdefault(key, []).append(rel)

        src_present, dst_present = (new, old) if args.revert else (old, new)
        if src_present and dst_present:
            blocked.append((rel, f"MIXED key sets ({len(src_present)} source, {len(dst_present)} target); refusing"))
            continue
        if not src_present:
            note = "already migrated" if dst_present else "no known component keys"
            plans.append((rel, path, header, body, maps, 0, note))
            continue
        plans.append((rel, path, header, body, maps, len(src_present), ""))

    width = max((len(p[0]) for p in plans + [(b[0],) for b in blocked]), default=20)
    for rel, path, header, body, maps, count, note in plans:
        detail = note if note else f"{count} distinct keys, {len(maps)} component maps"
        flag = "" if header else "  [headerless legacy file]"
        print(f"  {rel.ljust(width)}  {detail}{flag}")
    for rel, reason in blocked:
        print(f"  {rel.ljust(width)}  SKIPPED: {reason}")

    if all_unknown:
        print("\nkeys matching no registered component:")
        for key, where in sorted(all_unknown.items()):
            action = "will be DROPPED" if args.drop_unknown else "left as-is (pass --drop-unknown to remove)"
            print(f"  {key}  in {', '.join(where)}  [{action}]")

    todo = [p for p in plans if p[5] > 0]
    if not args.apply:
        print(f"\ndry run: {len(todo)} of {len(files)} files would change. Re-run with --apply.")
        print("Reminder: this must land in the same commit as the engine-side serializedKey change.")
        return

    if not todo and not (args.drop_unknown and all_unknown):
        print("\nnothing to do")
        return

    backup([p[1] for p in plans], args.force)

    total_remapped = total_dropped = 0
    for rel, path, header, body, maps, count, note in plans:
        remapped, dropped = remap(maps, mapping, args.drop_unknown, known)
        if remapped or dropped:
            write_file(path, header, body)
            total_remapped += remapped
            total_dropped += dropped
            print(f"  {rel.ljust(width)}  {remapped} keys remapped" + (f", {dropped} dropped" if dropped else ""))

    # verification: nothing from the source key set may survive anywhere
    stale = []
    for rel, path, *_ in plans:
        _, body = split_file(path)
        old, new, _unknown = classify(component_maps(body), forward, reverse)
        leftover = new if args.revert else old
        if leftover:
            stale.append((rel, sorted(leftover)))
    if stale:
        print("\nVERIFICATION FAILED, source-set keys survived:")
        for rel, keys in stale:
            print(f"  {rel}: {keys}")
        sys.exit(1)

    print(f"\n{total_remapped} keys remapped, {total_dropped} dropped, verified clean.")
    print(f"rollback: python scripts/migrate_component_keys.py --apply {'' if args.revert else '--revert'} --force")

if __name__ == "__main__":
    main()
