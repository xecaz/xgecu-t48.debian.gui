# xgecu-gui

A Debian/Linux GUI for the XGecu **T48 / T56 / TL866II+** universal device programmers, built on top of the open-source [minipro](https://gitlab.com/DavidGriffith/minipro) CLI/library. Aims to give Linux users a Qt6 desktop experience comparable to the proprietary Windows **Xgpro** application — using the same hardware, no Wine required.

License: **GPL-3.0-or-later** (inherited from linking minipro).

![Screenshot — writing an AT27C256 EPROM](screenshot.jpg)

**User guide:** [`docs/USAGE.md`](docs/USAGE.md) — step-by-step walkthrough of the GUI, including udev setup, ZIF placement, every device action, hex-editor power tools, and a troubleshooting section.

## Status

Verified end-to-end on a real T48 against a Microchip-fab AT27C256:
- ✅ Programmer auto-detect (T48 / T56 / TL866II+)
- ✅ Chip selection: 37k entries from the Windows DB, 32k of which minipro can program — the rest are shown grayed out with a "Windows-only" tag
- ✅ ZIF socket placement preview (with the IC↑ arrow + lever orientation)
- ✅ **Read**, **Verify**, **Blank check**, **Detect chip ID** (with reverse-lookup of mismatches)
- ✅ **Erase** (gated on `can_erase`; pre-erase blank-probe to refuse empty sockets)
- ✅ **Write** (size-strict, pre-write blank-probe, optional auto-verify)
- ✅ Editable hex view with cursor, undo/redo, red highlight on unsaved edits, Goto offset

Not yet:
- Data-memory tab for chips with separate EEPROM areas (AVR/PIC)
- Fuse / config-bit editor
- ICSP wiring and per-chip voltage adjustment
- Logic IC test, JEDEC PLD support
- `.deb` packaging

## Build (Debian 13)

```bash
sudo apt install build-essential cmake ninja-build pkg-config git \
    qt6-base-dev qt6-tools-dev qt6-base-dev-tools \
    libusb-1.0-0-dev zlib1g-dev python3-venv python3-lxml

# Clone with the minipro submodule:
git clone --recurse-submodules https://github.com/xecaz/xgecu-t48.debian.gui.git xgecu-gui
cd xgecu-gui
# If you already cloned without --recurse-submodules:
# git submodule update --init --recursive

# Python venv for the chip-DB merge script:
python3 -m venv .venv
.venv/bin/pip install -r scripts/requirements.txt

cmake -S . -B build -G Ninja
cmake --build build
./build/xgecu-gui
```

USB access: install minipro's udev rules so the device is accessible without
root.

```bash
sudo cp third_party/minipro/udev/60-minipro.rules /etc/udev/rules.d/
sudo cp third_party/minipro/udev/61-minipro-uaccess.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
# Then unplug and replug the programmer.
```

## Layout

- `src/core/` — non-GUI logic: `Programmer` + `ProgrammerWorker` (on a dedicated `QThread`), `ChipDatabase`, `BufferModel` (with dirty tracking).
- `src/ui/` — Qt widgets: `MainWindow`, `ChipSelectDialog`, `ZifSocketView`, `HexView`.
- `scripts/merge_chip_lists.py` — merges minipro's `infoic.xml` with the Windows `T48_List.txt` into `data/chips_merged.json`.
- `data/chips_merged.json` — generated chip catalog, embedded via Qt resources at build time.
- `third_party/minipro/` — git submodule of upstream [minipro](https://gitlab.com/DavidGriffith/minipro). Built by CMake (via its own Makefile) into `libminipro.a` and linked statically.
- `tests/` — Qt Test unit tests for the chip DB and buffer model.

## Credits

Created by **Xecaz** with a healthy dose of **Claude Code** in 2026.
