"""Writes the .def that exports every out-of-line public symbol of the given static libraries from the engine DLL.

Inline functions, template instantiations and inline variables sit in "pick any" / "pick largest" COMDAT sections; every
consumer compiles its own copy from the headers, so they are left out. That is what keeps the export count under 65535.

usage: gen_engine_def.py <dumpbin> <output.def> <dll name> <lib>...
"""
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

SYMBOL = re.compile(r"^[0-9A-F]{3,} ([0-9A-F]{8}) (SECT[0-9A-F]+|UNDEF|ABS|DEBUG)\s+(\w+)\s*(\(\))?\s+(External|Static)\s+\| (\S+)")
SELECTION = re.compile(r"^\s+Section length .* selection\s+(\d+)")
SKIPPED_SELECTIONS = {"2", "6"}


def ExportedSymbols(dumpbin, library):
    output = subprocess.run([dumpbin, "/nologo", "/symbols", library], capture_output=True, text=True, errors="replace", check=True).stdout
    symbols = {}
    inlineSections = set()
    lastSection = None
    for line in output.splitlines():
        if line.startswith("COFF SYMBOL TABLE"):
            inlineSections = set()
            lastSection = None
            continue
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


def main():
    dumpbin, output, dllName, libraries = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    symbols = {}
    with ThreadPoolExecutor() as pool:
        for librarySymbols in pool.map(lambda library: ExportedSymbols(dumpbin, library), libraries):
            symbols.update(librarySymbols)

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
    print(f"{output}: {len(symbols)} exports ({sum(1 for f in symbols.values() if not f)} data)")


if __name__ == "__main__":
    main()
