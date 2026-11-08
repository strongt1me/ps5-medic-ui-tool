# PS5 Medic

*[Deutsche Version](README.md)*

**PS5 Medic** checks the **live state** of a jailbroken PS5's homebrew setup (firmware, jailbreak chain, kstuff, ShadowMount+, payloads, files, configuration), explains known failure causes **in plain language** and repairs them where that is safe, **with backup and undo**.

It runs as a full-screen app directly on the console and is operated with the DualSense. One button: *Start check-up*.

> Status: version 0.4.0-dev. Tested on a PS5 Pro (FW 12.00) with ShadowMount+ 1.7beta11 and websrv v0.33. Other firmwares and setups are untested; that is why the rules are tied to versions and each one is marked *verified* or *not verified yet*.

## What it does

- **Check-up**: 26 rules (jailbreak chain and HEN, ShadowMount+, plugins and storage). The result is a list with severity, plain-language explanation, evidence (file/line/value) and source. The catalog is in [docs/RULES.md](docs/RULES.md) (German).
- **Repair** (5 fixes): rename an image (parentheses), set a ShadowMount+ option, remove BackPork from the autoload list, move a duplicate copy of the same TITLE_ID, guidance for nested images. A preview shows exactly what will change first.
- **Undo**: every repair is listed in the history and can be taken back.
- **Report**: the □ button in the findings list writes a Markdown report to `/data/ps5medic/reports/` and shows a short version as a **QR code** (scan it with your phone, copy the text, paste it into a forum or an issue). IP and MAC addresses, user and account IDs and serial numbers are removed; file and folder names stay in, please check before sharing. Error-like log lines that match no rule appear as **Unknown error lines** in the report: material for new rules.
- **Rule update**: Settings, Rule update. Put a new `rules.json` with its SHA-256 checksum on the console via FTP or a USB copy, **or** download it with □ from your own address (`update_url` in `/data/ps5medic/config.ini`; only accepted with a valid Ed25519 signature of the author). PS5 Medic verifies the file and installs it after ✕ (with backup and roll-back).
- German and English (Settings).

## Safety

- **Never deletes**, only moves or renames. Files that change are backed up first (`/data/ps5medic/backups/`), and every change is recorded in the journal (`/data/ps5medic/journal.jsonl`).
- **No writing without ✕**: a repair or an undo only happens after you confirm on the preview. Other writes: the report (□) and the rule update (✕ on the offer).
- Writes are limited to a whitelist (ShadowMount+ folder, autoload lists, your own scan paths, `/data/ps5medic`). Never under `/system`, `/system_ex` or `/dev`.
- **The network is only used for the rule download**, only after □ and a confirmation, only to the address in your `config.ini`, and nothing is sent except a GET request. (The check whether the ELF loader answers on port 9021 only goes to `127.0.0.1`.)
- No exploits, no firmware changes, no features to obtain, unlock or distribute games, keys or firmware. PS5 Medic repairs the **environment**, not content.

## Requirements

- An already jailbroken PS5 with an ELF loader (port 9021) and FTP (port 2121 here).
- To start it with the DualSense: [websrv](https://github.com/ps5-payload-dev/websrv) with its homebrew launcher (tested: v0.33). Only its "Big App" loader gives a payload the app context the console requires before it opens the controller (background: [docs/PHASE3.md](docs/PHASE3.md), German). Without websrv only the diagnostic payload (`ps5medic.elf`) runs.

## Installation

1. Copy the `PS5MEDIC` folder from the release to `/data/homebrew/` via FTP (it contains `ps5medicui.elf`, `ps5medic.elf`, `rules.json`, `homebrew.js` and the icon).
2. Start `websrv` (Payload Manager, autoloader or port 9021).
3. Open the **PS5 Medic** tile in the launcher (`http://<PS5-IP>:8080`). Alternatively from a PC: `make launch-ui PS5_HOST=<PS5-IP>`.

Note: when it starts a homebrew app, `websrv` ends a running Big App, which includes a game.

**Optional: tile under “Media”.** A tiny starter title (`FAKE42007`) puts PS5 Medic on the home screen; it asks `websrv` to start the UI. Building and installing: [docs/TITLE.md](docs/TITLE.md) (German; needs a ps5-native-app-boilerplate checkout; not part of the release package).

## Controls

| Screen | Buttons |
|---|---|
| Start | ↑↓ select · ✕ open · ◯ quit |
| Findings | ↑↓ select · ✕ details · △ repair/guide · □ report · ◯ back |
| Detail | △ repair · ◯ back |
| Repair preview | ✕ confirm (writes) · ◯ cancel |
| History | ↑↓ select · ✕ undo (asks first) · ◯ back |
| Report | ◯ back |

## Diagnostic payload

`ps5medic.elf` runs without a UI and is **read-only**: it writes `/data/ps5medic/last_check.txt` and exports the state as a snapshot to `/data/ps5medic/snapshots/`. On a PC you can check a snapshot with `medic-cli check` (fixture folder, see below).

## Building from source

Linux or WSL, CMake, a C++20 compiler. The core and the rules also run on a PC against fixture folders.

```bash
make test        # unit, rule, UI and report tests against all fixtures
make host        # medic-cli and the PC preview of the UI
```

For the console you need the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (`PS5_PAYLOAD_SDK`) and SDL2 for the PS5; build instructions are in [docs/PHASE3.md](docs/PHASE3.md) (German).

```bash
make ps5-ui                         # full-screen UI (ps5medicui.elf)
make ps5                            # diagnostic payload (ps5medic.elf)
make launch-ui PS5_HOST=192.168.1.x # upload and start through websrv
make dist                           # release folder and zip under dist/ (local only)
```

PC tools (`build/medic-cli`):

```bash
medic-cli check   <snapshot-folder>             # findings
medic-cli report  <snapshot-folder> [--qr]      # report, optionally a QR code in the terminal
medic-cli fix     <snapshot-folder> <rule-id>   # show the plan (run with --apply --yes, on a copy only!)
medic-cli history|undo|export ...
```

## Contributing

**Testers wanted** (other firmwares and setups): [docs/TESTER.md](docs/TESTER.md) (German, with an English summary). Report problems with the issue template and paste the report (□).

A new rule needs: an entry in `knowledge/rules.json` with a source, a positive and a negative fixture under `tests/fixtures/`, and an `expect.json`. The tests enforce both. Fixtures are **synthetic**: real file and game names do not belong in the repository.

## Documentation

| File | Contents |
|---|---|
| `CLAUDE.md` | Entry point for Claude Code: goal, stack, limits, workflow (German) |
| `docs/SPEC.md` | Features, controls, non-goals (German) |
| `docs/ARCHITECTURE.md` | Modules, data flow, rule format, safety (German) |
| `docs/RULES.md` | Rule catalog (26 rules) with sources, status and deviations (German) |
| `docs/ROADMAP.md` | Phases 0-5 (German) |
| `docs/PHASE2.md` to `PHASE5.md` | Console and snapshot, UI and app context, report and release, rule update and community (German) |
| `docs/TESTER.md` | Contributing as a tester (German, English summary) |
| `docs/TITLE.md` | Tile under “Media” (starter title; German) |
| `knowledge/` | Rules (`rules.json`) and JSON schema |

## License and credits

PS5 Medic is licensed under the **GNU General Public License, version 3 or (at your option) any later version** (GPL-3.0-or-later). The license text is in [LICENSE](LICENSE); every own source file carries an SPDX line. This includes the rule database `knowledge/rules.json`. Reason: the finished ELF links against the GPLv3 runtime libraries of the ps5-payload-sdk. The third-party components (MIT, zlib, BSD-2-Clause, DejaVu) are compatible with the GPL-3.0 and are listed with their licenses in [THIRD_PARTY.md](THIRD_PARTY.md).

The repository is **private** and there is no public release (author's decision, 8 Nov 2026). Anyone who passes on a package from `make dist` must also offer the matching source code under the GPL.

PS5 Medic is a community project and not affiliated with Sony. Use it at your own risk.
