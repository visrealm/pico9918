# size_report.py
#
# Report per-symbol code/data sizes from a built ELF, sorted largest first.
# Used to find inlining/cloning bloat and cold-in-flash candidates.
#
# Groups by CODE vs DATA using nm's type letter rather than by section name -
# the Pico SDK's copy_to_ram linker script merges __time_critical_func()
# code into the .data output section, so filtering by section name alone
# silently misses it: the two largest functions in the whole firmware,
# pico9918_scan_line and OutputSprites, both live in .data.
#
# Copyright (c) 2025 Troy Schrapel
#
# This code is licensed under the MIT license
#
# https://github.com/visrealm/pico9918
#

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

CODE_TYPES = set("tTwW")


def find_tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found

    home = os.environ.get("USERPROFILE") or os.environ.get("HOME") or ""
    for candidate in sorted(glob.glob(os.path.join(home, ".pico-sdk", "toolchain", "*", "bin", name + "*"))):
        return candidate

    sys.exit(f"error: couldn't find '{name}' - add the arm-none-eabi toolchain to PATH "
              f"(see env.bat) or pass --toolchain-bin")


def resolve_elf(path: str) -> str:
    if os.path.isfile(path):
        return path
    if os.path.isdir(path):
        matches = glob.glob(os.path.join(path, "**", "*.elf"), recursive=True)
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            # e.g. the RP2350 stage2 bootloader stub also builds a tiny .elf
            # alongside the real firmware - the largest one is the firmware.
            largest = max(matches, key=os.path.getsize)
            print(f"note: multiple .elf files found under {path}, using largest: {largest}",
                  file=sys.stderr)
            return largest
    sys.exit(f"error: no .elf found at/under '{path}'")


def read_sections(objdump: str, elf: str):
    out = subprocess.run([objdump, "-h", elf], capture_output=True, text=True, check=True).stdout
    sections = {}
    pattern = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+\S+")
    for line in out.splitlines():
        m = pattern.match(line)
        if not m:
            continue
        name, size_hex, vma_hex = m.groups()
        size = int(size_hex, 16)
        vma = int(vma_hex, 16)
        sections[name] = (vma, vma + size, size)
    return sections


def read_symbols(nm: str, elf: str):
    out = subprocess.run([nm, "--print-size", elf], capture_output=True, text=True, check=True).stdout
    symbols = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) != 4:
            continue
        addr_hex, size_hex, sym_type, name = parts
        try:
            addr = int(addr_hex, 16)
            size = int(size_hex, 16)
        except ValueError:
            continue
        symbols.append((addr, size, sym_type, name))
    return symbols


def section_for_addr(sections, addr):
    for name, (start, end, _size) in sections.items():
        if start <= addr < end:
            return name
    return "?"


def is_noise(name: str) -> bool:
    return (name.endswith("_veneer")
            or name.startswith("__flashcode_")
            or name.startswith("__flashdata_")
            or name.startswith("."))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report per-symbol sizes from a built PICO9918 ELF, largest first.",
        epilog="GitHub: https://github.com/visrealm/pico9918")
    parser.add_argument("elf", help=".elf file, or a build directory to search for one")
    parser.add_argument("-g", "--group-sections", action="store_true",
                        help="group by section instead of code/data, auto-discovering every "
                             "section that has symbols (largest section first)")
    parser.add_argument("-s", "--sections",
                        help="comma-separated section names to group by, e.g. "
                             "-s .flashcode,.flashcode_sdk (implies --group-sections)")
    parser.add_argument("-n", "--limit", type=int, default=0,
                        help="max rows per group (default: unlimited)")
    parser.add_argument("--toolchain-bin", help="directory containing arm-none-eabi-* tools")
    args = parser.parse_args()

    if args.toolchain_bin:
        nm = os.path.join(args.toolchain_bin, "arm-none-eabi-nm")
        objdump = os.path.join(args.toolchain_bin, "arm-none-eabi-objdump")
        size_tool = os.path.join(args.toolchain_bin, "arm-none-eabi-size")
    else:
        nm = find_tool("arm-none-eabi-nm")
        objdump = find_tool("arm-none-eabi-objdump")
        size_tool = find_tool("arm-none-eabi-size")

    elf = resolve_elf(args.elf)
    sections = read_sections(objdump, elf)
    symbols = read_symbols(nm, elf)

    if args.group_sections or args.sections:
        # group by section: either every section with symbols (auto-discovered,
        # largest section first), or an explicit comma-separated list
        auto_discover = not args.sections
        if auto_discover:
            wanted = [name for name, (_start, _end, size) in sections.items() if size > 0]
            wanted.sort(key=lambda name: -sections[name][2])
        else:
            wanted = [s.strip() for s in args.sections.split(",") if s.strip()]
        groups = {name: [] for name in wanted}
        for addr, size, _sym_type, name in symbols:
            if size == 0 or is_noise(name):
                continue
            section = section_for_addr(sections, addr)
            if section in groups:
                groups[section].append((size, name, section))
        # auto-discovery pulls in debug/bookkeeping sections with no real
        # symbols (just section-size accounting) - drop the empty ones so
        # they don't drown out sections that actually matter
        group_order = [g for g in wanted if not auto_discover or groups[g]]
    else:
        # default: group by code vs data, regardless of which section it's in
        groups = {"code": [], "data": []}
        for addr, size, sym_type, name in symbols:
            if size == 0 or is_noise(name):
                continue
            section = section_for_addr(sections, addr)
            kind = "code" if sym_type in CODE_TYPES else "data"
            groups[kind].append((size, name, section))
        group_order = ["code", "data"]

    for group in group_order:
        rows = sorted(groups.get(group, []), reverse=True)
        total = sum(size for size, _name, _section in rows)
        print(f"\n=== {group} ({total} bytes across {len(rows)} symbols) ===")
        if args.limit:
            rows = rows[:args.limit]
        for size, name, section in rows:
            print(f"{size:8d}  {section:<16s}  {name}")

    print(f"\n=== section summary ===")
    sys.stdout.flush()
    subprocess.run([size_tool, "-A", elf])

    return 0


if __name__ == "__main__":
    sys.exit(main())
