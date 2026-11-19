# PS5 Medic – Specification

[Deutsch](../SPEC.md) · **English**

## 1. Problem
Jailbreak setups on the PS5 consist of many parts (exploit chain, HEN/kstuff, ShadowMount+, Payload Manager, plugins) that work together depending on version and firmware. Typical errors (kernel panic, game does not start, image is not mounted) end up as the same cries for help in forums and GitHub issues again and again. The cause is often a config line, a file name or a version combination.

## 2. Goal
One button press on the PS5 → "Check-up" → list of findings with traffic light, plain-language explanation, source and – where possible – a "Repair" button.

## 3. Target audience
- Beginners who only know "it doesn't work".
- Advanced users who need a clean report for forums/issues.

## 4. Functions

### 4.1 Check-up (MVP)
- Detects: firmware, console model, running payloads/processes, HEN (etaHEN / kstuff-lite / HyperHEN / OnionHEN), versions, ShadowMount+ (version, `config.ini`, `debug.log`, `manual.lst`), storage locations, scan paths.
- Evaluates rules from `knowledge/rules.json`.
- Shows findings with severity: 🔴 critical · 🟠 warning · 🔵 note.
- Every finding: title, plain-language text, evidence found (line/file), affected versions, source (link), recommended step.

### 4.1a User interface (as of 12 Nov 2026)
- Splash screen at start (`ui/assets/splash.jpg`, embedded in the executable; any button skips it), then the start screen.
- **Five languages:** Deutsch, English, Français, Italiano, Español (Settings; the choice is stored in `/data/ps5medic/config.ini`, key `language`). Without a saved choice the app takes over the console's system language at start (German, English, French, Italian, Spanish); any other system language and any read error results in English (Marc, 12 Nov 2026). Knowledge texts (rules, error codes, self-tests) for fr/it/es are in `knowledge/l10n/`; reports and the dynamic texts of the repair preview (hints, notes, warnings, error messages) exist in all five languages (fragment table `libmedic/src/core_l10n.cpp`, report skeleton in `libmedic/src/report.cpp`); `medic-cli` on the PC only German and English.
- **Per game:** title list with sources, system database, required firmware, backport (also built into the game), file check, console errors, save data and possible reasons.
- Runs on firmwares 4.00 to 14.10 (as far as the jailbreak runs there); so far only FW 12.00 has been tested.

### 4.2 Repair (15 fixes and 1 hint)
| Fix ID | Action | Reversible by |
|---|---|---|
| `rename_image_parentheses` | Remove brackets from `.ffpfs`/`.ffpfsc` file names | Renaming back from the journal |
| `rename_case_prx` | `Il2cppUserAssemblies.prx` → `Il2CppUserAssemblies.prx` (only on the internal SSD, where the spelling matters) | Renaming back from the journal |
| `smp_set_key` | Set a single key in `/data/shadowmount/config.ini` (e.g. `kstuff_game_auto_toggle=0`) | Backup of the config.ini |
| `smp_disable_conflicting_backpork` | Disable BackPork autostart if `backport_fakelib=1` | Backup of the autoload list |
| `move_duplicate_titleid` | Move a duplicate source with the same TITLE_ID to `.duplicates/` in the scan path (SM+ does not scan dot folders) | Move back |
| `smp_translate_v1_keys` | Transfer flat v1 lines in a v2 `config.ini` (which SM+ 1.7beta12 ignores) into the correct section | Backup of the config.ini |
| `autoload_insert_delay` | Insert `!4000` after elfldr in the autoload list | Backup of the autoload list |
| `autoload_remove_duplicates` | Remove duplicate lines of an autoload list (the first stays) | Backup of the autoload list |
| `autoload_fix_missing` | Autoload entry without a file: correct the spelling, otherwise comment out (WebKit list) or remove (Payload Manager) | Backup of the autoload list |
| `pldmgr_enable_autoload` | Set `AUTOLOAD_ENABLED=1` in `/data/pldmgr/pldmgr_config.txt` | Backup of the file |
| `quarantine_broken_elf` | Move an unusable ELF file to `.medic_broken/` next to it (whitelist folders only, never delete) | Move back |
| `quarantine_empty_image` | Move an empty or aborted image (under 512 bytes) to `.medic_broken/` next to it (whitelist folders only, never delete) | Move back |
| `quarantine_leftovers` | Move leftovers of aborted copy/pack operations (`.part`, `.tmp`, `.teil` ... directly in the scan paths) to `.medic_broken/` in the same folder (never delete; size check in case the file is still growing) | Move back |
| `quarantine_partial_folder` | Move a half-finished pack folder (zstd volumes without `ampr_zstd.index`) directly in a scan path as a whole folder to `.medic_broken/` in the same scan path (never delete) | Move back |
| `chmod_program_files` | Set `eboot.bin` and modules without execute permission (`CE-107750-0`) to 0777; the journal remembers the old mode of each file | Restore old permissions |
| `fix_image_nesting_hint` | Hint + instructions only (no auto-fix, images are not changed) | – |

### 4.3 Report
- Export as text/Markdown to `/data/ps5medic/reports/` and as a **QR code** on the screen (scan with a phone → copy the report).
- Anonymisation: IPs, account IDs, serial numbers are removed.

### 4.4 Later (v2+)
- Live monitor: detect kstuff/SM+ crashes in real time and show the cause.
- ~~Rule update (manual, signed)~~ and ~~comparison "before/after" of two check-ups~~ have been finished since phase 5 (`docs/en/PHASE5.md`).
- Taking part: collect findings without a rule ("unknown error lines") and export them as an issue template.

## 5. Operation (full screen, controller)
- **Start screen:** big button "Start check-up", below it the latest results.
- **Findings list:** ✕ opens details, △ Repair, □ report, ◯ back.
- **Detail:** evidence, explanation, source, steps. Repair shows exactly what will be changed **beforehand**.
- **History:** list of all repairs with "Undo".
- **Error codes:** reference (list, details with explanation, steps and source), read-only; see `docs/en/RULES.md`, "Looking up error codes". If a finding names a known code, □ in the finding detail jumps to the entry. In the entry: □ source as a QR code, △ note the code for the report, after a check-up "What Medic sees right now".
- Language: German, English, French, Italian, Spanish (strings in JSON, `ui/i18n/`, knowledge texts in `knowledge/l10n/`).

## 6. Non-goals
- No jailbreak, no payload loader replacement, no game installer.
- No game, key or firmware functions.

## 7. Success criteria
- Check-up < 5 s on the console.
- Every MVP rule has at least one fixture + test.
- No fix without backup; Undo restores the original state bit-exactly (test).
