# Phase 3 – Fullscreen UI

[Deutsch](../PHASE3.md) · **English**

As of 8 Nov 2026. The platform-independent part is done and tested on the PC. `make ps5-ui` builds `build-ps5/ps5medicui.elf` (SDL2 + gamepad integration, see below). On the console the UI runs when it is started through websrv's big-app loader (`make launch-ui`): according to `ui.log`, picture and controller both work (see "First console run"). The whole flow was played through on the test console on 8 Nov (check-up, detail, repair preview, repair, undo, another check-up), evidenced by `ui.log` and the journal; Phase 3 is thereby complete.

## What is already there (`ui/`)
- `Canvas` (1920×1080, ARGB, anti-aliasing), controller symbols as vector shapes, PNG output for preview and tests.
- Font: DejaVu Sans, pre-rendered (`tools/gen_font.py` → `ui/src/font_data.cpp`). Umlauts, Latin-1 and all characters from `rules.json`. Unknown characters appear as `?`. Text wrapping also breaks long paths without spaces.
- Texts in `ui/i18n/de.json` and `en.json`, embedded via `tools/embed_i18n.py` (a test compares the file with the embedded text).
- `App`: nine screens (Home, Check-up running, Findings, Detail, Repair preview, Result, History, Undo prompt, Settings). Operation as in `SPEC.md`: ✕ open/confirm, △ repair, ◯ back. □ "Report" was added in Phase 4 (`docs/en/PHASE4.md`).
- `IMedicService`: the UI's only interface to the core. `LocalService` calls the core in the same process (PC preview, tests, **and** on the console – see below).
- The repair preview shows beforehand exactly what will be changed (moving with source and destination, writing with a line diff). Writing happens only after ✕ on the preview, undo only after ✕ on the confirmation prompt. The tests check that every other button writes nothing.
- Hint fixes (`*_hint`) show instructions instead of "Repair".

Preview on the PC (writes PNGs, changes nothing):
```bash
make preview SNAP=tests/fixtures/b8_pos OUT=build/preview
build/medic-ui-preview <snapshot> <ausgabe> --lang en --scale 2
```

## Correction: no jailbreak request needed

The first plan assumed that PS5 Medic had to be a proper, signed Sony "title" (its own library entry, `param.json`, FSELF) – with all the consequences: app sandbox, hence no access to `/data`, and a way to raise privileges (Lapy JB daemon or etaHEN API, see `CLAUDE.md`). That was wrong.

**The diagnostic payload from Phase 2 proves the opposite:** `build-ps5/ps5medic.elf`, which ran on the console on 8 Nov, reads `/data`, `/mnt/usb0` and `/mnt/shadowmnt` freely – entirely without a jailbreak request. That works because it is started through the Payload Manager/elfldr, not through the system library. These "payloads" are in principle not sandboxed; that is the normal model for kstuff, ShadowMount+, the Payload Manager itself and every other homebrew on the console.

The sandbox – and with it the Lapy/etaHEN detour – applies only to real titles started from the games library. PS5 Medic, however, needs no library entry, only a fullscreen display. So: **PS5 Medic is built as a payload**, with the already working `make ps5` pipeline (ps5-payload-sdk, full C++20 with regex/filesystem/streams – this already builds, see Phase 2). Result:
- no Lapy daemon, no jailbreak-request logic,
- no foreign, restricted build pipeline (a second attempt via `ps5-native-app-boilerplate` failed on exactly that: according to its own documentation, its minimal runtime stub forbids streams/filesystem/regex without separate verification – precisely what `libmedic` needs),
- `LocalService` remains the service implementation, on the console too (no `RemoteService`/transport needed).

`CLAUDE.md` still lists the Lapy/etaHEN route as the "Privileges" plan; that is now outdated and will be corrected when convenient.

**Addendum (console test, 8 Nov): one point was not quite right.** A payload started through elfldr (port 9021, also the Payload Manager) is indeed unsandboxed, but it has no **app context** (`app_id=0`), and without that the DualSense cannot be opened (`scePadOpen` → `0x809B0081`). The picture (VideoOut) works without it. Solution without a title and without a jailbreak request: **websrv's big-app loader** (`/hbldr`, ps5-payload-dev/websrv, GPL-3.0, runs on the console, not part of this repository). It starts a real system app via `sceSystemServiceLaunchApp("FAKE00000")` and swaps its content for the ELF via ptrace: unsandboxed **and** with app context. That is how LakeSnes, Mednafen and FFplay run with a controller too. websrv creates the fake app `/system_ex/app/FAKE00000` itself on the first homebrew start; it already exists on the test console, PS5 Medic writes nothing there. A title starter like `bfstarter` (`starter.c` only sends to port 9021) would **not** have solved the problem; the earlier idea of a "title starter for a library entry" is therefore dropped for the pad, and the launcher entry (tile) now comes from `homebrew.js`.

**Addendum (9 Nov 2026): websrv is no longer needed.** The big-app loader (`hbldr.c`, `elfldr.c`, `pt.c`) is now built into the launcher `ps5medic-launcher.elf` (port-9021 payload, `ps5/launcher_main.cpp`, loader source in `third_party/websrv-hbldr/`). It also creates the tile and starts the UI itself when needed. Structure, safety rules and console test: `docs/en/TITLE.md`. `make launch-ui` (websrv) remains as a fallback route.

**Addendum (12 Nov 2026): the tile no longer needs a homebrew loader either.** The tile is now a native starter title (`title/starter.c`, `eboot.bin`) that sends the launcher to port 9021 instead of calling websrv or opening the browser (the former service on port 9056 is dropped). There is a folder app `FAKE42007` under `/data/homebrew/` (ShadowMount+ creates the tile); `ps5medic-launcher.elf` creates the folder itself when it is sent. Everything is in `docs/en/TITLE.md`.

**Addendum (12 Nov 2026): splash screen, three more languages.**
- *Splash screen:* `ui/assets/splash.jpg` (1920×1080, JPEG) is embedded in the executable via `.incbin` (`ui/src/splash.cpp`, decoded with stb_image, JPEG/PNG only, `third_party/stb/`). `App::show_splash` displays it for about 2.7 s; any button skips it.
- *Languages:* `Lang` knows de, en, fr, it, es (`ui/include/medic_ui/i18n.hpp`). Interface: `ui/i18n/<language>.json`; knowledge texts for fr/it/es as a supplement in `knowledge/l10n/<language>.json` and `<language>.errors.json` (rules, reliability, coverage, measured values, self-tests, error codes), embedded via `tools/embed_i18n.py`. If an entry is missing, English applies. Reports (`ReportLang` de/en/fr/it/es; the data texts come from `ReportInput::l10n`, built from the same translation) and the dynamic fix texts (`core_l10n.cpp`) are likewise available in all five languages. `tools/l10n_extract.py` pulls the sources, `tools/l10n_check.py` checks a translation. The text rule (never recommend a system software update) applies in all five languages (`libmedic/src/text_policy.cpp`, test over all files). The title font contains the accented letters (`tools/gen_font.py`). The app saves the chosen language in `/data/ps5medic/config.ini` (key `language`). Without a saved choice, the console's system language applies at startup (`sceSystemServiceParamGetInt` with `PARAM_ID_LANG`, mapping in `lang_from_system_id`, `ui/src/i18n.cpp`); languages without a translation and read errors result in English. The service writes the system language it read to `/data/ps5medic/service.log` (line "Systemsprache"). The app's status lines before the service (e.g. "Service file missing") and its notifications follow the same language: the app has no import for the system language, it reads `language=` from `config.ini` and otherwise the file `/data/ps5medic/system_language`, which the service writes on every start; before the first service start and without either file, English applies (`ps5/native_ui_main.cpp`, table `kMsgs`).

## VideoOut and gamepad integration: SDL2

Decision: **SDL2** via [`ps5-payload-dev/SDL`](https://github.com/ps5-payload-dev/SDL) (zlib license, same organization as the SDK) instead of raw `sceVideoOut*`/`scePad*` syscalls by hand – a ready-made, tested VideoOut implementation instead of reconstructing structure layouts from memory.

**Building SDL2 for the PS5** (once, not part of `make ps5-ui`, no network access needed for normal builds):
```bash
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# 1. libsamplerate 0.2.2 (BSD-2-Clause) -- Abhaengigkeit von SDL2s Audiopfad
wget https://github.com/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz
tar -xf libsamplerate-0.2.2.tar.xz && cd libsamplerate-0.2.2
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared
make && make install && cd ..

# 2. SDL2 (ps5-payload-dev-Fork, zlib-Lizenz) -- mit einer Aenderung (siehe unten): 32 statt 64 MB Bildspeicher
git clone https://github.com/ps5-payload-dev/SDL.git && cd SDL
sed -i 's|device_data->memsize = 0x4000000;|device_data->memsize = 0x2000000; /* ps5-medic: 32 statt 64 MB */|' \
    src/video/ps5/SDL_ps5video.c
"$CMAKE" -DCMAKE_BUILD_TYPE=Release -DSDL_OPENGL=YES -DSDL_LOADSO=YES -B build -S .
"$MAKE" -C build && "$MAKE" -C build install

"$PS5_PAYLOAD_SDK/bin/prospero-cross-fix-root"
```
Ends up in `$PS5_PAYLOAD_SDK/target/user/homebrew/{include,lib}` – the Makefile finds it there automatically (`PS5_HOMEBREW`). Built this way on 8 Nov 2026 and linked with `ps5medicui.elf`.

**Modified SDL (zlib clause 2: mark modified versions as such):** The VideoOut driver (`src/video/ps5/SDL_ps5video.c`) requests a fixed 64 MB of direct memory at startup. The test console (PS5 Pro, FW 12.00) would not provide that (`EAGAIN`; 32, 24 and 16 MB worked, for all memory types). Two 1080p frame buffers need only 17 MB (`PS5_Tilemap_BufferSize`: 8.5 MB per buffer), so the `sed` step above sets `memsize` to 32 MB. Otherwise the driver is unchanged.

## First console run (8 Nov 2026)

Found during the first start on the test console (192.168.1.94):
- **Nothing visible, no tile:** The missing tile is intentional (payload, not a title). That nothing appeared was due to SDL's 64 MB memory request (see above): `SDL_Init` failed in `sceKernelAllocateMainDirectMemory`, and the payload then hung invisibly in an endless loop.
- **Troubleshooting without a screen:** `klog` (port 3232) delivered nothing here. The payload's `stdout` arrives only on the sending connection to port 9021, and only as long as it is kept open (`shutdown(SHUT_WR)` instead of `close`). The reliable source is the log file `/data/ps5medic/ui.log` (fetch via FTP; overwritten on every start, so a second start overwrites the log of the still-running first one).
- **All payloads are called `payload.elf`** in the process list (elfldr assigns the name), not like the ELF file. A "terminate other instances of myself" by process name is therefore not possible.
- **Only one instance:** As long as a UI is running, it holds VideoOut and direct memory; a second one fails with `sceVideoOutOpen: Device busy`. Via websrv, `/hbldr` terminates the running big app itself before starting the new one. For starts via port 9021, each instance additionally enters its PID in `/data/ps5medic/ui.owner` and exits as soon as a foreign PID is found there (checked every 15 frames); the new instance retries `SDL_Init` for up to 6 s.
- **Picture worked, DualSense did not (as a payload via elfldr):** `scePadOpen(user, 0, 0)` → `0x809B0081`, even with made-up user IDs; index 1 → `0x809B0003`, system IDs 255/254/−1 → `0x809B0001` (argument check). The user is found (`sceUserServiceGetLoginUserIdList`), but `sceUserServiceGetInitialUser` returns `0x80940004`. Evidence (throwaway probes, not in the repo): `sceKernelGetAppInfo` reports `app_id=0`, an empty title ID, `authid=0x480000001000000e` (like `pldmgr.elf`) for the payload, whereas the shell processes have real app IDs (SceShellUI `NPXS40087`). Starting via the Payload Manager changes nothing (it also starts via `elfldr`, ppid 103).
- **Whole flow confirmed on the console:** `ui.log` shows the screen sequence Home → Check-up → Findings (2) → Detail → Repair preview → Result → Check-up → Findings (1) → … → Findings (0), plus History → Undo prompt → Result. The journal `/data/ps5medic/journal.jsonl` has the transactions with intent, op, commit and undo; the files on the stick match the journal. **Real changes on the USB stick** were made and undone in the process: `…/homebrew/backport_aus/PPSA11111` → `…/homebrew/_duplicates/PPSA11111` (rule `smp.duplicate_titleid`) and `Rennspiel (5).ffpfsc` → `Rennspiel 5.ffpfsc` (rule `smp.parentheses_in_pfs_name`, rename only, 60.5 GB, no copy). Final state: both repairs applied, another check-up finds nothing; can be undone at any time via "History".
- **Solved via websrv `/hbldr`:** the same pad probe, started this way, yields a valid handle from `scePadOpen`, `rc=0` and `connected=1` from `scePadReadState`, and `GetForegroundUser` works. The UI reports `Controller verbunden: Yolo` there (name of the logged-in user) and outputs the first frame. Flow with `make launch-ui PS5_HOST=<ip>` (prerequisite: `websrv.elf` is running, port 8080; from the autoloader or via port 9021).
- **Exiting as an app:** Via websrv's loader the UI is a running app as far as the system is concerned. If only the process ended, the app stayed stuck in the foreground, and the only way out was the PS button and "Close". The UI now closes its own app on exit (`sceSystemServiceKillApp` with the app ID from `sceKernelGetAppInfo(getpid())`; as a payload with app ID 0 nothing happens, a foreign app can never be hit this way). See `docs/en/TITLE.md`.
- **Bug found and fixed: undo without a check-up.** New session, straight into the history, undo → "Path not in the whitelist": the whitelist came from the scan paths of the facts of the last check-up, which are empty in a new session. `LocalService` now collects the facts before repair and undo if none exist yet (`ensure_facts`); a test replays the new session (red without the fix, green with it).
- **Exit:** ◯ on the home screen quits the app (`App::quit_requested()`), `SDL_Quit` releases screen and memory. On errors the payload exits with a log entry instead of hanging.

**`ps5/ui_main.cpp`:** SDL window (1920×1080, fullscreen) ↔ `Canvas::data()` via `SDL_CreateRGBSurfaceWithFormatFrom`/`SDL_BlitSurface`, `SDL_GameController` ↔ `Button` (button mapping from `SDL_ps5joystick.c`: a=Cross, b=Circle, x=Square, y=Triangle, start=Options, shoulder buttons/D-pad directly). `LocalService` with `Ps5Platform(writable=true)`, tick ~30 frames/s.

**Controls (since 9 Nov 2026):** D-pad **and left stick** select; the direction logic is platform-independent in `ui/include/medic_ui/pad_nav.hpp` (tested on the PC). The stick counts from roughly 50 % deflection (`kOn` 16000), falls back only below `kOff` 9000, and only the stronger axis applies. Held up/down repeats after 300 ms every 70 ms (D-pad as well as stick; faster since 13 Nov 2026, previously 400/110); left/right remain single steps (otherwise the language flickers). **L1/R1** page: one page in the findings list and history, in the details to the previous/next finding (left/right too there), on Home and in Settings like up/down (on Home, left/right jump between History and Settings). Stick and shoulder buttons never confirm anything: repairing and undoing remain reserved for Cross alone (after the preview). The first stick deflections appear as `Stick-Achse …` in `ui.log`. Previously the UI evaluated only the D-pad (axis events were never read, L1/R1 arrived, but `App::handle` ignored them). Rules from the same two candidate paths as the diagnostic payload.

```bash
make ps5-ui
make deploy-ui PS5_HOST=<ip>   # ps5medicui.elf, rules.json, homebrew.js und Icon nach /data/homebrew/PS5MEDIC/
make launch-ui PS5_HOST=<ip>   # deploy-ui + Start über websrv (App-Kontext, nötig fürs Pad)
```
Starting also works without Make: in the websrv launcher (`http://<ip>:8080`) the tile "PS5 Medic" appears (`ps5/homebrew/homebrew.js`, icon from `tools/gen_icon.py`).

## Next steps
1. Start without a PC: an entry in the launcher is enough; websrv must be running for that (present in the test console's autoloader as a `websrv` ELF, check after a restart).
2. Phase 4 (report, anonymization, QR code, README, release package): see `docs/en/PHASE4.md`. The "PPSA folder/param.json" installation from the plan is dropped: the launcher entry (`ps5/homebrew/`) replaces it.
3. ~~Missing rules A2, A4, C1–C3~~ done (22 rules, see `docs/en/RULES.md`, "Addendum").

## Addendum: design with symbols (11 Nov 2026)

The interface has its own symbol set (`ui/src/icons.cpp`, `ui/include/medic_ui/icons.hpp`): 31 vector symbols (lines, circles, fills, cut-outs), each computed once per size into an opacity map and afterwards only tinted. No image files, nothing is loaded, no font needed. `draw_icon_badge` places a symbol on a rounded color field with a gradient. The symbol sheet for visual inspection is produced with `medic-ui-preview --icons <datei.png>`.

- **Home (as of 17 Nov 2026, simplified):** one status field ("What now?": result of the last check-up in one sentence and the next step) and three tiles: Games, History, More (Repairs, Self-test, Rule update, Backups and undo, Report, Error codes, Settings); **□** re-checks at any time. The following text describes the earlier state with seven tiles. **Earlier:** a large tile with a history, stethoscope and heartbeat line; five tiles (History, Settings, Error codes, Self-test, Games) with a colored symbol field; "Last check-up" with a large status symbol (check mark, warning, error, info). A partial check stays blue ("not everything read"), never green.
- **Header:** logo (cross in a color field), firmware chip. **Hint bar** at the bottom on a single surface. **Background:** gradient with two soft light spots, dithered (no visible steps), built once and copied per frame.
- **Check-up running:** stethoscope and a running heartbeat line instead of the bar.
- **Findings and detail:** a symbol per severity (Critical, Warning, Info), wrench or light bulb for repair or guide respectively. **History, Settings, Error codes, result screens:** title with symbol field, status symbols per row.
- **Self-test:** a symbol per test (drive, thermometer, database, file, speedometer, Wi-Fi, flame, chip, pen, controller, display) and a status symbol per result (check mark, warning, cross, dash, open circle). Rows are tighter so that all eleven tests fit above the hint bar.
- Tests: `icon_tests` in `tests/test_ui.cpp` (every symbol visible, in the field, accent, gradient).

## Addendum: history of the check-ups, each saved individually (11 Nov 2026, request from Marc)

Instead of resetting the history, **every check-up is saved as its own file** `/data/ps5medic/checks/<time>.json` (`libmedic/include/medic/checks.hpp`): findings with text (de/en), evidence, repair offer and the history status at the time (new/recurring), plus firmware, model, rule version, coverage, resolved rules and measured values. An existing file is never overwritten (same time: suffix `_2`), nothing is deleted; the list shows the 100 most recent, older ones remain as files. The files are only in the app's own folder and may contain paths and title names from the evidence (like the reports). The payload for diagnosis (read-only) saves none.

- **Home → History** opens the list (newest first, with the number of findings per severity; "no findings" is green only if everything was checked). **Cross** shows a check-up as it was at the time (findings list and details; the notice "State of …" at the top). **Triangle** compares it with the one before (newly added, resolved, unchanged, by rule). **Square** shows the repairs as before (journal, undo). Circle goes back one level each; an old state does not displace the session's current check-up (it returns with Circle).
- **View and compare, do not repair (decision by Marc):** In the old state, repair, guide and report are locked; the detail says that repairing is only possible in the current check-up, so that repairs are never made on an outdated state.
- The reset of the history (built shortly before, commit `5cda74a`) has been removed again; `history.jsonl` (comparison, urgency, measured-value level) and `journal.jsonl` (repairs) remain unchanged.
- Tests: `checks_archive_tests` in `tests/test_ui.cpp` (saving, loading, order, same time, damaged file, comparison, operation).


## Addendum: "Per game" view (11 Nov 2026, request from Marc)

Question from Marc: "Does the app also investigate when a game does not start, and what the reason is?" The rules (`knowledge/rules.json`) already recognize the known causes; new is the per-title view, so that you do not have to search the whole findings list for a title ID. **Home → Games** shows the list (titles with findings first, worst first, then by title ID; without a finding it says "No trace found", never "fine"). **Cross** opens a title, **L1/R1** pages to the previous/next one, **Circle** goes back. If there is no check-up in the session yet, it runs first and the list appears afterwards.

- **Core:** `libmedic/include/medic/games.hpp`, `libmedic/src/games.cpp` (`collect_game_infos`). Nothing is newly collected and nothing is written; the function summarizes the facts and findings of the last check-up: sources (folder/image, in use, duplicate), registration in the system database (only if it was read; otherwise "Not checked"), required firmware with backport, file checks (`eboot.bin`, upper/lower case, over-allocation), the console's error history for the title, savegames with a foreign account ID and the last four lines of the ShadowMount+ log with the title ID. Findings belong to a title if its title ID or the path of one of its sources appears in the evidence. The name comes from the `param.json` (`games.fw[].name`, new) and stays empty if none is known.
- **Interface:** `Screen::GameList`, `Screen::GameDetail`. On the left, what Medic found for the title; on the right "Possible reasons" = the title's findings. Cross opens a finding in the familiar detail screen (repair as there; Circle goes back to the game view, after a repair to the findings list). Home now has five small tiles (Games on the far right).
- **Limits:** Medic only sees traces that exist on the console. A crash without a trace goes undetected; the view says so ("That does not mean everything is fine"). It works on the session's check-up, not on old states from the history.
- Tests: `games_tests` in `tests/test_ui.cpp` (merging with synthetic titles, sorting, unknown registration, operation with the fixture `g1_pos`, empty list). Names and paths in tests and preview are synthetic.

### Before/after comparison extended (16 Nov 2026)

- `compare_checks` additionally reports **severity changes** (same rule, different level; no longer listed under "unchanged") and **changed setup**: firmware, model and rule version from each check-up, plus the new, optional field `setup` of the check-up file (ShadowMount+ version, kstuff version, number of games, ampr-emu-ng versions; text only, no paths or titles). Old files without `setup` remain readable; their missing entries do not count as "new".
- **Direct entry:** Left on the findings screen compares the current check-up with the one before; Circle leads back to the findings list. Read-only, no repair, no network.
- Test: `checks_archive_tests` (severity change, setup, old file, saving and loading `setup`).

### Support package (16 Nov 2026)

- On the report screen, Triangle creates a **support package**: `/data/ps5medic/support/support_<time>.zip` (ZIP without compression, `libmedic/src/support.cpp`) with `README.txt`, `report_de.md`, `report_en.md`, `facts.json` (the facts read about games, Kraken, kstuff) and, if there is an earlier check-up, `compare.txt` (new, resolved, severity, setup).
- **Anonymized** like the report (IP/MAC, account IDs, serial numbers) and additionally **pseudonymized**: title IDs (also PPSA/CUSA identifiers in the text) become `[TITLE-nn]`, names of games, game folders and images become `[GAME-nn]`. The **key** (placeholder → original) lies only as a separate file `support_<time>_key.txt` next to it and never belongs in the package. Short (under 4 characters) and generic names ("games", "data" …) are not replaced.
- Writes only new files in its own folder (never overwrite, never delete), no network, no game content. Timestamps in the ZIP fixed to 1980-01-01.
- Tests: `support_tests` in `tests/test_report.cpp` (CRC check value, ZIP self-check, no name/ID/address in the package, key only outside) and the flow in the UI test; cross-check with `python3 -m zipfile -t` and `unzip -t`.

### "Resolved by repair" (16 Nov 2026)

- `repairs_between` (`libmedic/src/checks.cpp`) links a **resolved rule** with the **journal**: for each rule, the most recent transaction that is completed (`applied`), not undone or aborted, and whose time lies **after the earlier and at most at the later check-up**. Without an earlier check-up or without a matching transaction (fixed by hand, by an update) there is no entry; nothing is made up.
- Shown in the **comparison** ("Finding … – by repair: <name>"), in the **report** (`## Resolved since the last check`, with fix ID and time) and in `compare.txt` of the support package. For this, the report uses `ReportInput::prev_ts` (time of the check before).
- Read-only: the journal is only read. Tests: `checks_archive_tests` (time window, undone, aborted, most recent per rule) and `support_tests` (report).

### Hiding findings (16 Nov 2026)

- Warnings and notes that you cannot fix yourself can be **hidden** (Marc, 16 Nov): **Options** on the findings list or in the detail. **Critical findings cannot be hidden.** On the findings list a chip "Hidden n" is shown; **Right** opens the list of hidden findings, **Cross** there shows one again.
- Stored per **rule** in `/data/ps5medic/ignored.json` (`libmedic/src/ignore.cpp`): rule ID, severity when hidden, time, active. **Nothing is deleted**: "Show again" sets the entry to inactive. Without the file or with broken content, nothing is hidden.
- **Only the display** changes. Saved check-ups, history, report, support package and the comparison still contain **all** findings (Medic must not conceal anything). The check after a repair ("resolved?") also sees a hidden rule.
- **If the severity rises** (note → warning → critical), the finding reappears: hidden applies only to the severity at the time and below. An old check-up (History view) always shows all findings.
- Tests: `ignore_tests` (rules, saving, loading, broken file), UI flow in `tests/test_ui.cpp`, saving via the service.
