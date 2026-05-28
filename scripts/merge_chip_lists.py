#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Merge minipro's infoic.xml (programmable chips, with parameters) with the
Windows Xgpro T48_List.txt (all chips the official app supports, name-only)
into one JSON catalog the GUI loads at startup.

Output schema: data/chips_merged.json
{
    "schema_version": 1,
    "generated_from": [...],
    "chips": [
        {
            "manufacturer": "ATMEL",
            "name": "ATMEGA328P",
            "package": "DIP28",
            "supported": true,
            "minipro_db": "INFOIC2PLUS",
            "minipro_name": "ATMEGA328P @DIP28",
            "type": 2,
            "protocol_id": "0x71",
            "pin_count": 28,
            "pin_map": "0xd000b700",
            "package_details": "0x00004000",
            "adapter": 0,
            "icsp": 0
        },
        ...
    ]
}
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable

from lxml import etree

# Bit-field layout of the package_details attribute. Mirrors database.c.
PIN_COUNT_MASK = 0x3F000000
ICSP_MASK = 0x0000FF00
ADAPTER_MASK = 0x000000FF
PLCC_ADAPTERS = {
    0x38: 20,
    0x3D: 44,
    0x3E: 28,
    0x3F: 32,
}


def decode_package_details(raw: int) -> tuple[int, int, int]:
    """Return (pin_count, adapter, icsp). Mirrors get_pin_count in minipro."""
    adapter = raw & ADAPTER_MASK
    icsp = (raw & ICSP_MASK) >> 8
    pin_count_raw = (raw & PIN_COUNT_MASK) >> 24
    # PLCC adapters override the pin_count bits.
    pin_count = PLCC_ADAPTERS.get(adapter, pin_count_raw)
    return pin_count, adapter, icsp


def parse_infoic_xml(path: Path) -> dict[tuple[str, str, str], dict]:
    """
    Parse infoic.xml into {(manuf, name_upper, package_upper) -> chip_dict}.

    We pull chips from the INFOIC2PLUS section, which is what T48/T56/TL866II+
    share. Names in the XML use the form "NAME@PACKAGE" embedded in the
    `name` attribute (e.g. "ATMEGA328P@DIP28"); some lack the @ entirely.
    """
    out: dict[tuple[str, str, str], dict] = {}
    tree = etree.parse(str(path))
    for db in tree.getroot().iter("database"):
        db_type = db.get("type", "")
        if db_type != "INFOIC2PLUS":
            continue
        for manuf_el in db:
            if manuf_el.tag not in ("manufacturer", "custom"):
                continue
            manuf = (manuf_el.get("name") or "").strip()
            for ic in manuf_el.iter("ic"):
                full_name = (ic.get("name") or "").strip()
                if not full_name:
                    continue

                pkg_details_raw = int(ic.get("package_details", "0x0"), 16)
                pin_count, adapter, icsp = decode_package_details(pkg_details_raw)
                shared = {
                    "manufacturer": manuf,
                    "supported": True,
                    "minipro_db": db_type,
                    "type": int(ic.get("type", "0")),
                    "protocol_id": ic.get("protocol_id", ""),
                    "pin_count": pin_count,
                    "pin_map": ic.get("pin_map", "0x0"),
                    "package_details": ic.get("package_details", "0x0"),
                    "adapter": adapter,
                    "icsp": icsp,
                }

                # minipro packs aliases as a comma-separated list in `name`,
                # each alias optionally suffixed with @PACKAGE. minipro's
                # search_chip_name (database.c:544) tokenizes on ',' and
                # matches a single alias case-insensitively — so we must
                # store one alias per row, not the whole bundle.
                for alias in full_name.split(","):
                    alias = alias.strip()
                    if not alias:
                        continue
                    if "@" in alias:
                        name, _, pkg = alias.partition("@")
                    else:
                        name, pkg = alias, ""
                    name = name.strip()
                    pkg = pkg.strip()
                    entry = dict(shared, name=name, package=pkg,
                                 minipro_name=alias)
                    key = (manuf.upper(), name.upper(), pkg.upper())
                    out[key] = entry
    return out


# Matches one "CHIP_NAME @PACKAGE" cell. Chip names have no whitespace; package
# tokens may contain '-' and digits.
_CELL_RE = re.compile(r"(\S+?)\s+@(\S+)")
_SECTION_RE = re.compile(r"^\s*\[\s*(.+?)\s*\]\s*$")
_FOOTER_RE = re.compile(r"^\s*IC Support:.*PCS\s*$", re.IGNORECASE)


def parse_t48_list(path: Path) -> Iterable[tuple[str, str, str]]:
    """Yield (manufacturer, name, package) tuples from the Xgpro list."""
    manuf = ""
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            s = line.rstrip("\n")
            m = _SECTION_RE.match(s)
            if m:
                manuf = m.group(1).strip()
                continue
            if _FOOTER_RE.match(s):
                continue
            for name, pkg in _CELL_RE.findall(s):
                yield manuf, name.strip(), pkg.strip()


def merge(infoic_path: Path, t48list_path: Path) -> dict:
    minipro = parse_infoic_xml(infoic_path)
    merged: dict[tuple[str, str, str], dict] = {}

    # Seed with all minipro chips (supported by definition).
    for key, entry in minipro.items():
        merged[key] = entry

    # Add Xgpro list entries; mark unsupported if minipro has no matching key.
    seen_in_txt = 0
    only_in_txt = 0
    for manuf, name, pkg in parse_t48_list(t48list_path):
        seen_in_txt += 1
        key = (manuf.upper(), name.upper(), pkg.upper())
        if key in merged:
            continue
        merged[key] = {
            "manufacturer": manuf,
            "name": name,
            "package": pkg,
            "supported": False,
        }
        only_in_txt += 1

    chips = sorted(
        merged.values(),
        key=lambda c: (c["manufacturer"].upper(), c["name"].upper(), c["package"].upper()),
    )
    return {
        "schema_version": 1,
        "generated_from": [str(infoic_path), str(t48list_path)],
        "stats": {
            "minipro_chips": len(minipro),
            "txt_entries_seen": seen_in_txt,
            "txt_unique_unsupported": only_in_txt,
            "total": len(chips),
        },
        "chips": chips,
    }


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--infoic", required=True, type=Path)
    p.add_argument("--t48list", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args(argv)

    result = merge(args.infoic, args.t48list)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        json.dump(result, fh, ensure_ascii=False, separators=(",", ":"))
    stats = result["stats"]
    print(
        f"wrote {args.out} ({stats['total']} chips: "
        f"{stats['minipro_chips']} supported, "
        f"{stats['txt_unique_unsupported']} Windows-only)",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
