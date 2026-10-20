"""Writes the .def that exports every out-of-line public symbol of the given object files from the engine DLL.

Inline functions, template instantiations and inline variables sit in "pick any" / "pick largest" COMDAT sections; every
consumer compiles its own copy from the headers, so they are left out. That is what keeps the export count under 65535.

Each object's symbols are cached by timestamp in <output.def>.cache.json, so a build only runs dumpbin on the objects that changed.

usage: gen_engine_def.py <dumpbin> <output.def> <dll name> <object list file>
"""
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SYMBOL = re.compile(r"^[0-9A-F]{3,} ([0-9A-F]{8}) (SECT[0-9A-F]+|UNDEF|ABS|DEBUG)\s+(\w+)\s*(\(\))?\s+(External|Static)\s+\| (\S+)")
SELECTION = re.compile(r"^\s+Section length .* selection\s+(\d+)")
SKIPPED_SELECTIONS = {"2", "6"}
DUMP_HEADER = "Dump of file "
BATCH_SIZE = 32


def ParseSymbols(lines):
    symbols = {}
    inlineSections = set()
    lastSection = None
    for line in lines:
        selection = SELECTION.match(line)
        if selection:
            if lastSection and selection.group(1) in SKIPPED_SELECTIONS:
                inlineSections.add(lastSection)
            continue
        match = SYMBOL.match(line)
        if not match:
            continue
        value, section, _, isFunction, storage, name = match.groups()
        if storage == "Static":
            lastSection = section
            continue
        # An UNDEF with a size is a C tentative definition (volk's function pointers)
        bCommon = section == "UNDEF" and int(value, 16) != 0
        if not bCommon and (not section.startswith("SECT") or section in inlineSections):
            continue
        # Compiler-generated: string literals, float constants, anonymous namespaces, lambdas, exception and RTTI tables
        if name.startswith(("??_C@", "__real@", "__xmm@", "__ymm@", "_TI", "_CT", "_CTA", "__catch", "__unwind", "$")) or "?A0x" in name or "<lambda" in name:
            continue
        symbols[name] = isFunction is not None
    return symbols


def DumpBatch(dumpbin, objects):
    output = subprocess.run([dumpbin, "/nologo", "/symbols", *objects], capture_output=True, text=True, errors="replace", check=True).stdout
    results = {}
    current = None
    lines = []
    for line in output.splitlines():
        if line.startswith(DUMP_HEADER):
            if current is not None:
                results[current] = ParseSymbols(lines)
            current = os.path.normcase(os.path.abspath(line[len(DUMP_HEADER):].strip()))
            lines = []
            continue
        lines.append(line)
    if current is not None:
        results[current] = ParseSymbols(lines)
    return results


def main():
    dumpbin, output, dllName, objectList = sys.argv[1:5]
    with open(objectList, encoding="utf-8") as file:
        objects = [line.strip() for line in file if line.strip().endswith(".obj")]

    cachePath = output + ".cache.json"
    cache = {}
    if os.path.exists(cachePath):
        with open(cachePath, encoding="utf-8") as file:
            cache = json.load(file)

    stamps = {}
    stale = []
    for path in objects:
        key = os.path.normcase(os.path.abspath(path))
        stat = os.stat(path)
        stamps[key] = [stat.st_mtime_ns, stat.st_size]
        entry = cache.get(key)
        if entry is None or entry[0] != stat.st_mtime_ns or entry[1] != stat.st_size:
            stale.append(path)

    batches = [stale[i:i + BATCH_SIZE] for i in range(0, len(stale), BATCH_SIZE)]
    with ThreadPoolExecutor() as pool:
        for results in pool.map(lambda batch: DumpBatch(dumpbin, batch), batches):
            for key, symbols in results.items():
                cache[key] = stamps[key] + [symbols]
    cache = {key: cache[key] for key in stamps}
    with open(cachePath, "w", encoding="utf-8") as file:
        json.dump(cache, file)

    symbols = {}
    for entry in cache.values():
        symbols.update(entry[2])

    lines = [f"LIBRARY {dllName}", "EXPORTS"]
    for name in sorted(symbols):
        lines.append(f"    {name}" if symbols[name] else f"    {name} DATA")
    text = "\n".join(lines) + "\n"

    # Unchanged exports must not relink the DLL
    try:
        with open(output, encoding="utf-8") as existing:
            if existing.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(output, "w", encoding="utf-8") as file:
        file.write(text)
    print(f"{output}: {len(symbols)} exports ({sum(1 for f in symbols.values() if not f)} data), {len(stale)} of {len(objects)} objects read")


if __name__ == "__main__":
    main()
