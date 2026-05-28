# CLAUDE.md — xgecu-gui

A Qt6 / C++ Debian GUI for the XGecu **T48 / T56 / TL866II+** universal device programmers, built on top of the open-source [minipro](https://gitlab.com/DavidGriffith/minipro) library.

License: **GPL-3.0-or-later** (forced by static linkage against minipro). Add `// SPDX-License-Identifier: GPL-3.0-or-later` to every new source file.

Window title: `XGecu T-48/T-56/TL866II+ by Xecaz`. About box credits Xecaz + Claude Code, 2026.

## Layout

- `src/core/` — non-Qt-Widgets logic (`Programmer`, `ProgrammerWorker`, `ChipDatabase`, `BufferModel`).
- `src/ui/` — Qt widgets (`MainWindow`, `ChipSelectDialog`, `ZifSocketView`, `HexView`).
- `third_party/minipro/` — **git submodule** of `https://gitlab.com/DavidGriffith/minipro.git`, pinned. Clone with `--recurse-submodules`.
- `scripts/merge_chip_lists.py` — combines `third_party/minipro/infoic.xml` + `../T48_List.txt` into `data/chips_merged.json`.
- `data/chips_merged.json` — generated chip catalog (~9 MB raw; gets Qt-resource-compressed in the binary). Committed for now so building doesn't require running the merge first.
- `tests/` — Qt Test unit tests (`test_chip_database`, `test_buffer_model`). Live-device tests would go under `tests/live/` gated by `XGECU_LIVE_TESTS=1`; none written yet.

## Build

Out-of-source build, Ninja generator:

```bash
git clone --recurse-submodules <repo> xgecu-gui && cd xgecu-gui
python3 -m venv .venv
.venv/bin/pip install -r scripts/requirements.txt
cmake -S . -B build -G Ninja
cmake --build build
./build/xgecu-gui
ctest --test-dir build --output-on-failure
```

The minipro static library is built via its own Makefile, invoked from CMake (`add_custom_command` → `make -C third_party/minipro library` via the resolved absolute GNU `make` path, not `${CMAKE_MAKE_PROGRAM}` since Ninja won't drive minipro's Makefile). Linker pulls in `libusb-1.0` and `zlib` via `pkg-config`.

## Hardware

`lsusb` shows `a466:0a53 "TL866II Plus Device Programmer [MiniPRO]"` — that VID/PID is **shared between TL866II+, T48, and T56**. The actual model is determined post-handshake by minipro and lives in `minipro_handle_t::version` (`MP_T48 = 7`). The dev machine has a real **T48** plugged in (verified with `minipro -k`).

USB access on Debian 13: user must be in `plugdev` AND the minipro udev rules must be installed (from `third_party/minipro/udev/`). The current dev machine has them in place — `/dev/bus/usb/.../<dev>` carries a `user:xecaz:rw-` ACL via `61-minipro-uaccess.rules`.

## Threading

**All `minipro_*` calls are blocking and must NEVER run on the GUI thread.** A dedicated `QThread` owns `ProgrammerWorker`, which holds the `minipro_handle_t*` for the lifetime of the connection. Communication is exclusively via queued signals/slots. Slots: `setInfoicPath`, `setLogicicPath`, `detect`, `openChip`, `readCode`, `verifyCode`, `eraseChip(force)`, `writeCode(data, force, autoVerify)`, `detectChipId`. Signals: `detected`, `chipOpened(name, codeSize, dataSize, canErase)`, `progress`, `readFinished`, `verifyFinished`, `chipIdFinished`, `eraseFinished`, `writeFinished`, `error`.

Cancellation is cooperative: a `std::atomic<bool>` checked between minipro block I/O calls. Block sizes are ≤4 KB.

## Critical minipro setup pitfalls

These each cost real debugging time the first time around — guard against regressing them:

- **`handle->cmdopts` must not be NULL.** `t48_begin_transaction()` reads `handle->cmdopts->icsp`, segfaulting if it's NULL. We allocate a zero-initialised `cmdopts_t` once per worker and assign it after every `minipro_open()` (and clear `handle->cmdopts` before `minipro_close` so it doesn't double-free).
- **`get_device_by_name()` needs both `infoic_path` AND `logicic_path`.** It internally opens `logicic.xml` first (database.c:1755). Without it, every chip lookup fails. MainWindow resolves both via `findMiniproFile()` and pushes them to the worker through `setInfoicPath` / `setLogicicPath` queued slots.
- **`search_chip_name()` matches one alias at a time.** minipro's XML packs aliases into one record's `name` attribute as a comma list (e.g. `"27C256@DIP28,27C256@SOIC28,27LV256@DIP28,27LV256@SOIC28"`). The merge script must store a **single** alias per row in `minipro_name`, not the bundle, because `search_chip_name` (database.c:544) `strcasecmp`s the user-supplied name against each comma-split token.
- **`setInfoicPath` / `setLogicicPath` must be slots**, not inline header methods, or `QMetaObject::invokeMethod` can't dispatch them and the paths silently never reach the worker.

## Chip database

`merge_chip_lists.py` produces a JSON catalog containing **every chip the Windows Xgpro lists** (37,103 entries in `T48_List.txt`) but flags those that minipro can actually program. After the alias fix the supported count is ~32,600 alias-expanded entries from ~11,500 unique XML records. Windows-only chips are surfaced in the UI grayed-out with a `supported: false` flag so users immediately know to fall back to Xgpro on Windows.

Each supported entry carries the minipro fields needed for runtime + ZIF rendering: `pin_count`, `pin_map`, `package_details`, `adapter`, `icsp`, `protocol_id`, and the per-alias `minipro_name` token. Bit layout of `package_details` mirrors `database.c`:
- bits 0–7  (`ADAPTER_MASK 0x000000ff`): adapter type (0 = direct ZIF; 1 = TSOP48 adapter; etc.).
- bits 8–15 (`ICSP_MASK 0x0000ff00`): ICSP flags.
- bits 24–29 (`PIN_COUNT_MASK 0x3f000000`): pin count (or PLCC adapter sentinel `0x38`/`0x3D`/`0x3E`/`0x3F` → 20/44/28/32 pins).

## ZIF socket preview (`ZifSocketView`)

Custom `QWidget` that paints a 48-pin ZIF socket and overlays the selected chip outline. The TL866-family convention for DIP packages (confirmed by photo on the user's T48): chip is **TOP-justified** in the socket, notch facing **UP** (away from the lever). Pin 1 of the chip aligns with ZIF pin 1 at the top-left, next to the silkscreened `IC↑` arrow on the case. For an N-pin DIP, the chip body occupies left-column ZIF pins `1..N/2` and right-column pins `(48-N/2+1)..48` (the top portion of the right column). A yellow dot marks chip pin 1 at the top-left of the chip body.

Lever: a thick (8 px) vertical metal stem exits just outside the right pin column, runs straight down, terminates in a round ball with a subtle radial gradient. No knob, no pill shape — that's not what the real lever looks like.

For adapters (`adapter != 0`) and PLCC packages, the current widget shows a textual hint — full adapter overlays are a TODO.

## Safety / "live" testing

The connected programmer has a real T48 plugged in. **Never run destructive operations without explicit user confirmation in this conversation.** Read-only operations safe in any phase: device-info, chip-ID read, code-memory read, verify, blank check. Erase / Write require interactive `QMessageBox` confirmation in the UI.

**T48 cannot electrically detect a missing chip.** `minipro_pin_test()` is only wired for TL866II+ and T76; for T48 there is no hardware presence check. As a software safety net both Erase and Write do a **pre-op probe**: read the first block of code memory, and if it's all `0xFF` (either an empty socket or an already-blank chip) refuse with an explanatory dialog. Both dialogs expose a `Force` checkbox / button to skip the probe when the user is sure. Write additionally skips the probe automatically when the *source buffer* is itself all `0xFF` (writing 0xFF into an empty socket is a no-op).

If the user has an original EEPROM in the socket without a backup, the rule is: **Read → save to file → ask the user to verify the saved image** before any destructive op. The user has a Microchip-fab AT27C256 in the socket — silicon-identical to ATMEL `AT27C256@DIP28` (chip_id `0x298C`), distinct from the Intel `27C256` entry (`0x898C`) and the Atmel `AT27C256R` (`0x1E8C`). This kind of variant mismatch is what `Detect chip ID` is for.

## Conventions

- C++20, `Q_OBJECT` widgets in `src/ui/`, plain `QObject`-derived workers in `src/core/`.
- Forward-declare Qt classes in headers where possible.
- `extern "C" { #include "minipro.h" }` only inside `.cpp` files; keep minipro out of headers (use opaque forward decl `struct minipro_handle;` etc.).
- One short comment only for non-obvious WHY (e.g. "TL866 convention: chip top-justified"). No file-banner comments beyond the SPDX line.
- New chips/protocols are minipro's domain — do NOT add custom chip definitions here; upstream them to minipro instead.
- Toolbar/menu wording trends short ("Read chip…", "Verify chip…") rather than verbose ("Read code memory against buffer…"). File menu has only Open/Save/Exit; toolbar has device actions only.

## Status (Phases 0–2 done)

- ✅ Bootstrap, CMake, libminipro static lib, chip DB merge with alias fix.
- ✅ Detect programmer, select chip (filtered tree with search + "show Windows-only" toggle).
- ✅ ZIF socket preview with IC↑ arrow and lever.
- ✅ Read code memory → buffer; file Open/Save.
- ✅ Verify code memory against current buffer.
- ✅ Blank check (reuses verify path with synthesised all-`0xFF` buffer).
- ✅ Detect chip ID (handles MP_ID_TYPE1/2/3/4/5; on mismatch, reverse-lookup via `get_device_from_id`).
- ✅ Erase, gated on `can_erase`, with pre-erase blank-probe + Force.
- ✅ Write code memory, strict size match, pre-write blank-probe + Force, optional auto-verify in the same transaction.
- ✅ Editable `HexView`: cursor, click-to-position, arrow/PgUp/PgDn/Home/End, Tab between hex and ASCII, hex-digit and ASCII typing, `QUndoStack`-backed edits, red highlight on unsaved edits (cleared by File Save or successful chip Write), Goto offset (Ctrl+G).

## Not yet

- Data-memory tab for chips with separate EEPROM areas (AVR/PIC).
- Fuse / config-bit editor (`fuse_decl_t` metadata is already in `device_t::config`).
- ICSP toggle + per-chip voltage / SPI-clock adjustment.
- Logic IC test, JEDEC PLD support.
- `.deb` packaging, .desktop entry, system icon.
- A `tests/live/` harness for hardware smoke tests gated by `XGECU_LIVE_TESTS=1`.
