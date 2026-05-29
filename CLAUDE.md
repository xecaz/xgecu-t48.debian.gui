# CLAUDE.md — xgecu-gui

A Qt6 / C++ Debian GUI for the XGecu **T48 / T56 / TL866II+** universal device programmers, built on top of the open-source [minipro](https://gitlab.com/DavidGriffith/minipro) library.

Released tag: **`v0.1.0`** (commit `282814d`). Verified end-to-end against a real T48 + Microchip-fab AT27C256 (UV-EPROM): read → save → load → verify → write → auto-verify round-trip works.

License: **GPL-3.0-or-later** (forced by static linkage against minipro). Add `// SPDX-License-Identifier: GPL-3.0-or-later` to every new source file.

Window title: `XGecu T-48/T-56/TL866II+ by Xecaz`. About box credits Xecaz + Claude Code, 2026.

## Layout

- `src/core/` — non-Qt-Widgets logic: `Programmer` + `ProgrammerWorker` (on a dedicated `QThread`), `ChipDatabase`, `BufferModel` (with dirty tracking + `dirtyChanged` signal).
- `src/ui/` — Qt widgets: `MainWindow`, `ChipSelectDialog`, `ZifSocketView`, `HexView`.
- `third_party/minipro/` — **git submodule** of `https://gitlab.com/DavidGriffith/minipro.git`, pinned. Clone with `--recurse-submodules`.
- `scripts/merge_chip_lists.py` — combines `third_party/minipro/infoic.xml` + `../T48_List.txt` into `data/chips_merged.json`.
- `data/chips_merged.json` — generated chip catalog (~9 MB raw; gets Qt-resource-compressed in the binary). Committed for now so building doesn't require running the merge first.
- `tests/` — Qt Test unit tests (`test_chip_database`, `test_buffer_model`).
- `tests/live/test_live_programmer.cpp` — live-hardware smoke tests, each method `QSKIP`s unless `XGECU_LIVE_TESTS=1`. The default `ctest` run stays green without hardware.
- `debian/` — packaging (`control`, `rules`, `changelog`, `copyright`, `source/format`, `postinst`, `postrm`). `dpkg-buildpackage -b -us -uc` builds the `.deb`.
- `packaging/xgecu-gui.desktop` — application launcher.
- `packaging/xgecu-gui.svg` — stylised DIP-package icon.

## Build

Out-of-source build, Ninja generator:

```bash
git clone --recurse-submodules <repo> xgecu-gui && cd xgecu-gui
python3 -m venv .venv
.venv/bin/pip install -r scripts/requirements.txt
cmake -S . -B build -G Ninja
cmake --build build
./build/xgecu-gui
ctest --test-dir build --output-on-failure              # unit + skipped live
XGECU_LIVE_TESTS=1 ./build/tests/test_live_programmer   # active live run
```

The minipro static library is built via its own Makefile, invoked from CMake (`add_custom_command` → `make -C third_party/minipro library` via the resolved absolute GNU `make` path, not `${CMAKE_MAKE_PROGRAM}` since Ninja won't drive minipro's Makefile). Linker pulls in `libusb-1.0` and `zlib` via `pkg-config`.

`cmake --install build --prefix /usr` (or `DESTDIR=…` for staging) lays the binary down under `/usr/bin/`, the bundled `infoic.xml` / `logicic.xml` under `/usr/share/xgecu-gui/`, the `.desktop` + `.svg` icon in the standard XDG paths, and minipro's udev rules under `/usr/lib/udev/rules.d/`.

## Hardware

`lsusb` shows `a466:0a53 "TL866II Plus Device Programmer [MiniPRO]"` — that VID/PID is **shared between TL866II+, T48, and T56**. The actual model is determined post-handshake by minipro and lives in `minipro_handle_t::version` (`MP_T48 = 7`). The dev machine has a real **T48** plugged in (verified with `minipro -k`).

USB access on Debian 13: user must be in `plugdev` AND the minipro udev rules must be installed (from `third_party/minipro/udev/`; or installed system-wide by the `.deb`). The current dev machine has them in place — `/dev/bus/usb/.../<dev>` carries a `user:xecaz:rw-` ACL via `61-minipro-uaccess.rules`.

## Threading

**All `minipro_*` calls are blocking and must NEVER run on the GUI thread.** A dedicated `QThread` owns `ProgrammerWorker`, which holds the `minipro_handle_t*` for the lifetime of the connection. Communication is exclusively via queued signals/slots.

The worker API is **`MemArea`-parametric** (Code/Data):

- Slots: `setInfoicPath`, `setLogicicPath`, `detect`, `openChip`, `readMemory(area)`, `verifyMemory(area, expected)`, `writeMemory(area, data, force, autoVerify)`, `eraseChip(force)`, `detectChipId`.
- Signals: `detected(DeviceInfo)`, `chipOpened(name, codeSize, dataSize, canErase)`, `progress(done, total)`, `readFinished(ReadResult{area, data})`, `verifyFinished(VerifyResult{area, …})`, `writeFinished(WriteResult{area, …})`, `chipIdFinished`, `eraseFinished`, `error`.

`MemArea::Code` maps to `MP_CODE`, `MemArea::Data` maps to `MP_DATA`. The word-organised address shift (`flags.data_org == MP_ORG_WORDS`) **only applies to code memory** — `verifyMemory` and `writeMemory` deliberately gate it on `area == MemArea::Code`.

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

## Code / Data tabs

Central widget is a `QTabWidget` with one `BufferModel` + `HexView` per `MemArea`. The Data tab is **hidden until** `openChip` reports `data_memory_size > 0`. Tab labels gain a leading `● ` when their buffer has unsaved edits (`BufferModel::dirtyChanged`).

`m_buffer` / `m_hex` in `MainWindow` are **aliases** refreshed by `onCurrentTabChanged()` so every lambda call site that operates on "the current buffer / view" keeps working without per-action plumbing. `currentArea()` derives the active area from the tab index. `paneFor(MemArea)` exposes the per-area buffer, view, chip size, and tab index.

Result handlers dispatch by `result.area`, not by the tab that's visible — so a Read on Code while the Data tab is showing will fill the Code buffer and switch to it.

## ZIF socket preview (`ZifSocketView`)

Custom `QWidget` that paints a 48-pin ZIF socket and overlays the selected chip outline. The TL866-family convention for DIP packages (confirmed by photo on the user's T48): chip is **TOP-justified** in the socket, notch facing **UP** (away from the lever). Pin 1 of the chip aligns with ZIF pin 1 at the top-left, next to the silkscreened `IC↑` arrow on the case. For an N-pin DIP, the chip body occupies left-column ZIF pins `1..N/2` and right-column pins `(48-N/2+1)..48` (the top portion of the right column). A yellow dot marks chip pin 1 at the top-left of the chip body.

Lever: a thick (8 px) vertical metal stem exits just outside the right pin column, runs straight down, terminates in a round ball with a subtle radial gradient. No knob, no pill shape — that's not what the real lever looks like.

For adapters (`adapter != 0`) and PLCC packages, the current widget shows a textual hint — full adapter overlays are a TODO.

## Hex editor (`HexView`)

`QAbstractScrollArea`-based, fully editable.

- **Cursor**: byte offset + column (Hex / ASCII) + nibble (high/low). Click-to-position, Tab to switch column.
- **Selection**: mouse-drag, Shift+arrow / Shift+PgUp / Shift+Home extend, Ctrl+A selects all. Translucent overlay; cursor (moving end) keeps the focus highlight.
- **Edits**: type hex digits in the Hex column (high then low → byte committed, cursor advances), printable bytes in the ASCII column. Backspace cancels a half-typed nibble or moves back. All edits go through `BufferModel::setByteAt` and are wrapped in `ByteEditCommand`s on `QUndoStack`.
- **Dirty bytes** painted in red (`#DC322F`) in both columns; cleared on `markClean()` (called after File Save and successful chip Write).
- **Fill range** (`Edit > Fill…`): start/end addresses + repeating byte pattern (hex, e.g. `EA F1 00 2C`). Live byte-count summary. Single `FillCommand` for one-step undo.
- **Copy range** (`Edit > Copy range…`): source range → destination offset. Source bytes are **snapshotted up-front** so overlapping ranges are safe. Single `CopyCommand`.
- **Find** (`Edit > Find bytes…`, Ctrl+F): hex / ASCII / decimal mode picker with live byte-preview. F3 reuses the *parsed* bytes (no re-parsing). Wrap detection: distinguishes "Match", "Search wrapped — no more instances after cursor", and "Only one match".
- **Go to offset** (Ctrl+G): decimal or `0x…` hex.

## Safety / "live" testing

The connected programmer has a real T48 plugged in. **Never run destructive operations without explicit user confirmation in this conversation.** Read-only operations safe in any phase: device-info, chip-ID read, code/data-memory read, verify, blank check. Erase / Write require interactive `QMessageBox` confirmation in the UI.

**T48 cannot electrically detect a missing chip.** `minipro_pin_test()` is only wired for TL866II+ and T76; for T48 there is no hardware presence check. As a software safety net both Erase and Write do a **pre-op probe**: read the first block of code memory, and if it's all `0xFF` (either an empty socket or an already-blank chip) refuse with an explanatory dialog. Both dialogs expose a `Force` checkbox / button to skip the probe when the user is sure. Write additionally skips the probe automatically when the *source buffer* is itself all `0xFF` (writing 0xFF into an empty socket is a no-op).

If the user has an original EEPROM in the socket without a backup, the rule is: **Read → save to file → ask the user to verify the saved image** before any destructive op. The user's working chip is a Microchip-fab AT27C256 — silicon-identical to ATMEL `AT27C256@DIP28` (chip_id `0x298C`), distinct from the Intel `27C256` entry (`0x898C`) and the Atmel `AT27C256R` (`0x1E8C`). This kind of variant mismatch is what `Detect chip ID` is for.

## Conventions

- C++20, `Q_OBJECT` widgets in `src/ui/`, plain `QObject`-derived workers in `src/core/`.
- Forward-declare Qt classes in headers where possible.
- `extern "C" { #include "minipro.h" }` only inside `.cpp` files; keep minipro out of headers (use opaque forward decl `struct minipro_handle;` etc.).
- One short comment only for non-obvious WHY (e.g. "TL866 convention: chip top-justified"). No file-banner comments beyond the SPDX line.
- New chips/protocols are minipro's domain — do NOT add custom chip definitions here; upstream them to minipro instead.
- Toolbar/menu wording trends short ("Read chip…", "Verify chip…") rather than verbose. File menu has only Open/Save/Exit; toolbar has device actions only. The Maximize/Fullscreen button was removed from the toolbar; Ctrl+M toggles maximize via `setWindowState(... ^ Qt::WindowMaximized)`. On GNOME the title-bar buttons obey `org.gnome.desktop.wm.preferences.button-layout` and the WM ignores our `WindowMaximizeButtonHint` — that's a user-side gsettings call (`gsettings set … button-layout 'appmenu:minimize,maximize,close'`), not something we can override.

## Status (Phases 0–2 complete; Data tab UI ready)

- ✅ Bootstrap, CMake, libminipro static lib, chip DB merge with alias fix.
- ✅ Detect programmer, select chip (filtered tree with search + "show Windows-only" toggle).
- ✅ ZIF socket preview with IC↑ arrow and lever.
- ✅ Read code/data memory → buffer; file Open/Save; drag-and-drop file open.
- ✅ Verify code/data memory against current buffer.
- ✅ Blank check (reuses verify path with synthesised all-`0xFF` buffer).
- ✅ Detect chip ID (handles MP_ID_TYPE1/2/3/4/5; on mismatch, reverse-lookup via `get_device_from_id`).
- ✅ Erase, gated on `can_erase`, with pre-erase blank-probe + Force.
- ✅ Write code/data memory, strict size match, pre-write blank-probe + Force, optional auto-verify in the same transaction.
- ✅ Editable `HexView` (cursor, selection, edit, undo, dirty highlight, Fill, Copy, Find, Goto).
- ✅ Code / Data tabs with per-tab dirty marker.
- ✅ `.deb` packaging (debian/ tree + .desktop + SVG icon + udev rules).
- ✅ Live-hardware test harness gated by `XGECU_LIVE_TESTS=1`.

## Not yet (paused — waiting on ATmega328P delivery)

- ATmega328P smoke test on real silicon: Detect chip ID returns `0x1E950F`, Code reads 32768 bytes, Data reads 1024 bytes, round-trip read/write/verify on both areas.
- Fuse / config-bit editor. `device_t::config` already carries `fuse_decl_t` metadata, but doing it blind would be guesswork — needs an AVR/PIC in the loop. ATmega328P is the canonical test case (LFUSE/HFUSE/EFUSE/LOCK).

## Niche / on-demand (skip until needed)

- ICSP toggle + per-chip voltage / SPI-clock adjustment.
- Logic IC test (7400-series gate testing).
- JEDEC PLD support (GAL/PAL programming).
