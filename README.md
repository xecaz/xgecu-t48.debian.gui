# xgecu-gui

A Debian/Linux GUI for the XGecu T48 / T56 / TL866II+ universal device programmers, built on top of the open-source [minipro](https://gitlab.com/DavidGriffith/minipro) CLI/library. Aims to provide a Qt6 desktop experience comparable to the proprietary Windows **Xgpro** application.

License: **GPL-3.0-or-later** (inherited from linking minipro).

## Status

Early development. See `/home/xecaz/.claude/plans/in-this-folder-is-glittery-wigderson.md` for the design.

## Build (Debian 13)

```bash
sudo apt install build-essential cmake ninja-build pkg-config git \
    qt6-base-dev qt6-tools-dev qt6-base-dev-tools \
    libusb-1.0-0-dev zlib1g-dev python3-venv python3-lxml

cmake -S . -B build -G Ninja
cmake --build build
./build/xgecu-gui
```

## Layout

- `src/core/` — non-GUI logic (programmer worker, chip database, buffer model).
- `src/ui/` — Qt widgets and dialogs.
- `third_party/minipro/` — vendored / submoduled minipro source.
- `scripts/merge_chip_lists.py` — build-time DB merge.
- `data/chips_merged.json` — generated chip database (committed for now).
- `tests/` — Qt Test unit tests + gated live-device tests.
- `udev/` — udev rules for non-root USB access (mirrors minipro's).
