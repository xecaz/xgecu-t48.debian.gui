# CLAUDE.md — xgecu-gui

A Qt6 / C++ Debian GUI for the XGecu **T48 / T56 / TL866II+** universal device programmers, built on top of the open-source [minipro](https://gitlab.com/DavidGriffith/minipro) library.

License: **GPL-3.0-or-later** (forced by static linkage against minipro). Add `// SPDX-License-Identifier: GPL-3.0-or-later` to every new source file.

## Layout

- `src/core/` — non-Qt-Widgets logic (`Programmer`, `ProgrammerWorker`, `ChipDatabase`, `BufferModel`).
- `src/ui/` — Qt widgets (`MainWindow`, `ChipSelectDialog`, `ZifSocketView`, `HexView`).
- `third_party/minipro/` — **symlink** to `/home/xecaz/haxx/xgecu-t48/minipro/` for now. Switch to a real git submodule before publishing.
- `scripts/merge_chip_lists.py` — combines `minipro/infoic.xml` + `T48_List.txt` into `data/chips_merged.json`.
- `data/chips_merged.json` — generated chip catalog (~23 MB raw, ~560 KB gzipped, embedded via Qt resources).
- `tests/` — Qt Test unit tests; live-device tests live under `tests/live/` and are gated by `XGECU_LIVE_TESTS=1`.

## Build

Out-of-source build, Ninja generator:

```bash
.venv/bin/pip install -r scripts/requirements.txt    # one-time; venv at .venv/
cmake -S . -B build -G Ninja
cmake --build build
./build/xgecu-gui
ctest --test-dir build --output-on-failure
```

The minipro static library is built via its own Makefile, invoked from CMake (`add_custom_command` → `make -C third_party/minipro library`). Linker pulls in `libusb-1.0` and `zlib` via pkg-config.

## Hardware

`lsusb` shows `a466:0a53 "TL866II Plus Device Programmer [MiniPRO]"` — that VID/PID is **shared between TL866II+, T48, and T56**. The actual model is determined post-handshake by minipro and lives in `minipro_handle_t::version` (`MP_T48 = 7`). The confirmed device on this machine is a **T48** (verified with `minipro -k`).

USB access on Debian 13: user must be in the `plugdev` group AND the minipro udev rules must be installed. The current dev machine has them in place — `/dev/bus/usb/.../<dev>` carries a `user:xecaz:rw-` ACL via `61-minipro-uaccess.rules` from `third_party/minipro/udev/`.

## Threading

**All `minipro_*` calls are blocking and must NEVER run on the GUI thread.** A dedicated `QThread` owns `ProgrammerWorker`, which holds the `minipro_handle_t*` for the lifetime of the connection. Communication is exclusively via queued signals/slots:

- UI → worker: `detect`, `open(chip)`, `read(area)`, `write(area, buf)`, `verify(area, buf)`, `erase`, `blankCheck`, `cancel`.
- Worker → UI: `detected(DeviceInfo)`, `progress(done, total)`, `operationFinished(ok, msg)`, `error(QString)`.

Cancellation is cooperative: a `std::atomic<bool>` checked between minipro block I/O calls. Block sizes are ≤4 KB, so latency is acceptable.

## Chip database

The merge script produces a JSON catalog containing **every chip the Windows Xgpro lists** (37,103 entries in `T48_List.txt`) but flags those that minipro can actually program (~11,500 unique XML records → ~32,600 alias variants in our current merge). Windows-only chips are surfaced in the UI with a `supported: false` flag so users immediately know to fall back to Xgpro on Windows.

Each supported entry carries the minipro fields needed for runtime + ZIF rendering: `pin_count`, `pin_map`, `package_details`, `adapter`, `icsp`, `protocol_id`, `minipro_name`. The bit layout of `package_details` mirrors `database.c`:
- bits 0–7  (`ADAPTER_MASK 0x000000ff`): adapter type (0 = direct ZIF; 1 = TSOP48 adapter; etc.).
- bits 8–15 (`ICSP_MASK 0x0000ff00`): ICSP flags.
- bits 24–29 (`PIN_COUNT_MASK 0x3f000000`): pin count (or PLCC adapter sentinel 0x38/3d/3e/3f → 20/44/28/32 pins).

## ZIF socket preview (`ZifSocketView`)

Custom `QWidget` that paints a 48-pin ZIF socket and overlays the selected chip outline. The TL866-family convention for DIP packages: chip is **bottom-justified** in the socket, with the chip notch facing the ZIF lever. For an N-pin DIP, the chip body occupies left-column ZIF pins `(24 - N/2 + 1)..24` and right-column pins `25..(24 + N/2)`. A yellow dot marks chip pin 1 at the bottom of the chip body.

This mirrors the chip-placement picture in the Windows Xgpro app. For adapters (`adapter != 0`) and PLCC packages, the current widget shows a placeholder hint — full adapter overlays are a TODO.

## Safety / "live" testing

The connected programmer has a real T48 plugged in. **Never run destructive operations without explicit user confirmation in this conversation.** Read-only operations safe in any phase: device-info query, chip-ID read, code-memory read. Erase / write / protect / blank-check require interactive `QMessageBox::warning` confirmation in the UI and an `XGECU_LIVE_TESTS=1` gate in tests.

If the user has an original EEPROM in the socket without a backup, do not click Erase or Write under any circumstance. Read → save-to-file → ask the user to verify the saved image before any destructive op.

## Conventions

- C++20, `Q_OBJECT` widgets in `src/ui/`, plain `QObject`-derived workers in `src/core/`.
- Forward-declare Qt classes in headers where possible.
- `extern "C" { #include "minipro.h" }` only inside `.cpp` files; keep minipro out of headers (use opaque forward decl `struct minipro_handle;`).
- One short comment only for non-obvious WHY (e.g. "TL866 convention: chip bottom-justified"). No file-banner comments beyond the SPDX line.
- New chips/protocols are minipro's domain — do NOT add custom chip definitions here; upstream them to minipro instead.

## Phased roadmap (see `/home/xecaz/.claude/plans/in-this-folder-is-glittery-wigderson.md`)

- **Phase 0** ✅ — bootstrap, CMake, link libminipro, chip DB merge.
- **Phase 1** (current) — detect device, chip select w/ ZIF preview, **Read code memory → file**.
- **Phase 2** — buffer load/save, Write, Verify, Erase, Blank Check; fuse editor.
- **Phase 3** — data-memory tab, ICSP, per-chip voltage, JEDEC, logic test, find/replace.
- **Phase 4** — `.deb` packaging, firmware update, multi-language.
