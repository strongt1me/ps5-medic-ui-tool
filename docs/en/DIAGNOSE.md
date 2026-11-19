# Diagnostic catalogue and build plan (as of 11 Nov 2026)

[Deutsch](../DIAGNOSE.md) · **English**

Goal: PS5 Medic becomes a complete system and game doctor that finds and explains hardware and software problems of a jailbroken console and, where safely possible, fixes them reversibly. The basis is research in ten sub-topics (hardware, system software, professional methodology, reverse-engineering techniques, the console file system read via FTP, the homebrew stack, Console Service Tool, tool catalogue, two source-code analyses of licensed tools). The detailed report lives outside the repository; this file records what has been built from it, what comes next and what will never be built.

## Guiding rules

1. **First the observed file, then the documented API, then the community report.** Anything that rests only on names in stubs is first proven in a child process on the test console.
2. **"Not checked" is a state of its own and never means "healthy"** (example: fact `integrity_skip` per database).
3. **Keep evidence apart:** observed on the console, documented, community, derived, unknown. Rules without evidence stay at `verified: todo`.
4. **Thresholds live only in `rules.json`** and are provisional until calibrated on the test console.
5. **Fixtures are synthetic** (invented title IDs, invented account IDs); real file names, titles and identifiers from the console are never in the repository.
6. The hard limits from `CLAUDE.md` apply unchanged; in particular, no text ever mentions a system software or firmware update (`text_policy.cpp`).

## Built (rules, collectors, tests)

| Area | Collector | Rules | Source of the idea |
|---|---|---|---|
| Save data and account | `collect_savedata` (ACCOUNT_ID from `savedata_prospero_meta`) | `savedata.foreign_account` | Observation on the test console |
| Boot history | `collect_system_logs` (`vshlog.0.txt`) | `sys.unclean_boot`, `sys.unclean_boot_history` | Exploration of the console (read-only) |
| Error history | `collect_system_logs` (`error/history/*.json`) | `errors.recurring_title`, `errors.recurring_system_app` | Exploration of the console |
| Databases, stage 1 | `collect_databases` (header, size, journal) | `db.header_invalid`, `db.truncated`, `db.journal_pending` | sqlite.org/fileformat2.html |
| Databases, stage 2 | `sqlite_check.cpp` (`integrity_check` on an in-memory copy, WAL included) | `db.integrity_failed` | Idea after PS5 Cleanup (GPL-3.0-or-later), own code |
| Error codes | Catalogue `error_codes.json` (150 codes) | – | Sony pages, update-free wording |
| Coverage | `evaluate_coverage` (list `coverage` in `rules.json`, 13 areas) | – (report, UI, CLI) | Guiding rule "not checked ≠ healthy" |
| Reliability | `confidence` per rule (level, basis, reason; default from `verified`) | – (UI, report, CLI) | Guiding rule "keep evidence apart" |
| Game folders | `collect_game_files` (file name wrong only in upper/lower case) | `game.il2cpp_case` (with fix `rename_case_prx`), `game.il2cpp_case_latent` | ShadowMount+ #159, #173 (originals read) |

Details and findings: `docs/en/RULES.md` (addenda of 10 and 11 Nov). On the test console, all new sources are readable from the payload without privilege escalation (verified).

## Build plan (order)

Safety ladder: read files, read known SDK sensors, explore the unknown in a child process, load, write in the own folder, copy foreign files into the own folder, config changes with backup.

| Batch | Contents | Status |
|---|---|---|
| 1 Foundation | **built:** collector coverage ("not checked", see `docs/en/RULES.md`); fix pipeline with preview (repair preview with diff, confirmation, journal, undo) and **re-check after the repair** (the UI says whether the rule is still reported); **confidence with reason** (`confidence`: level, basis, reason per rule; see `docs/en/RULES.md`); **rule pack governance** (format level `requires_level`, rollback protection already existed; expiry date deliberately not); **finding record** with `status`, `first_seen`, `seen_runs` and urgency (via the history, see batch 4) | **Batch 1 built** |
| 2 Observed sources | Boot history, error history, databases, error codes added; open: inventory (downloads, duplicate packages, orphans), shortcuts, log sizes | partly built |
| 3 Jailbreak stack | Rules with maintainer coverage, reversible fixes: **built** `game.il2cpp_case` (+ `_latent`); "autoload after kstuff" not evidenced in the original (see `docs/en/RULES.md`); **built** `smp.image_mount_0x60`, `smp.register_console_db`, `pldmgr.loose_elfs` (originals read); checked and deliberately not built: unplugging the drive, orphaned tile (already fixed), 0644/0755; further candidates only after reading the original | built for the evidenced candidates |
| 4 History | **built:** `history.jsonl` (rule IDs per check-up), comparison with the last check, "fixed since", urgency; **built:** measured values with previous value and usual level (median), `confirm_runs` (provisional → confirmed); **built (16 Nov):** before/after diff of severity and setup (firmware, rule version, component versions); **built (16 Nov):** link "fixed by repair" with the journal; measured values from sensors (batch 5) | **almost fully built** |
| 5 Sensors and thermals | **built:** proof of concept in a child process (`ps5/probe_sensors.cpp`, results in `docs/en/RULES.md`), relative temperature rule `sys.temp_above_usual` from the own history; signatures proven by disassembly, units not: clocks are configuration values, power fields fluctuate without relation to load, no measured value in the product (see `docs/en/RULES.md`); only the fan (not present in FW 7.61) and a second proof for the start/shutdown counters remain open | **Batch 5 completed** |
| 6 File and image plausibility | **built:** exFAT length against file size, empty images, `eboot.bin` (ELF/SELF start, empty, text); the ELF check of the payloads already existed, the save data collector reads PSF; open: PKG, TRP, PFS/PFSC (no primary source for the headers, see `docs/en/RULES.md`) | partly built |
| 7 Self-test framework | **built (v1):** registry with 5 read-only tests, status model, `test.*` facts, markers `selftest.running`/`selftest.interrupted`, UI screen, report, export (see `docs/en/RULES.md`); open: `needs_tests`/`confirmed_by_test`, progress with remaining time | built |
| 8 Active tests | **built:** step-by-step sessions with cancel, Thorough level with confirmation, heat test (short CPU load, abort at 85 °C), write test (one file, 256 MiB, never delete, one run per day, 50 GiB budget); **additionally built:** memory pattern test, calculation error detection under load, user tests for pad buttons and display, fan controller hint; open: audio test (D14), runs of the memory test on the console (only with approval) | largely built |
| 9 After proof or decision | **open, each blocked** (as of 11 Nov 2026): title matching in `app.db` (**built except D17**: only titleId and path, see `docs/en/RULES.md`); NVMe health (no evidenced access without privilege escalation, no known call); klog (built: reading, filtering and collecting per console start, see D10 and D10b); LAN test (**built, local only**: connection attempts to the gateway, see D1 and `docs/en/RULES.md`); UART log import and NOR info in the PC companion (D4–D6: only ideas from the Console Service Tool, data would have to come from primary sources, which are missing); audio test (D14: no proof of sound output); runs of the Thorough tests on the console (only with approval per run) |

## Decisions (provisionally set, Marc can change any)

The conservative recommendations from the report are set; they change nothing about today's behaviour.

| No. | Topic | Status |
|---|---|---|
| D1 | Internet network tests | **no**, only local measurements (the rule "no network to the outside" stays); **exception (Marc, 11 Nov): local network test `sw.net.local`, only connection setup to the default gateway in the private address range**; never a LAN PC or the internet |
| D2 | Safe mode hints | "Reset/reinstall" stays excluded; "rebuild database" only with a caveat (effect on titles mounted via ShadowMount+ unchecked) and with "back up save data first", as in the `db.*` texts |
| D3 | `kernel_dynlib_resolve` as import probe | **not a product feature**; privilege escalation only in the tile launcher, never in sensors or checks |
| D4–D6 | UART error codes, NOR summary, Console Service Tool | only later in the PC companion (`medic-cli`), not in the console UI; from the Console Service Tool only ideas and non-protectable facts (no licence), acknowledgement yes |
| D7 | Calibration of the thresholds | Measurement series on the test console before every rule via `info`; `sys.unclean_boot` (3 in 14 days) is provisional |
| D8 | Write test | **Marc: overwrite one file in the own folder, never delete**; 256 MiB, 1 GiB per run, one run per day are suggestions |
| D9 | Thorough level | only after batch 7, with explicit confirmation |
| D10 | Reading `/dev/klog` ourselves | **approved by Marc (11 Nov)**, built via `klogsrv` (port 3232), consuming, filtered, without payload output; D10b (collecting per console start in `klog_boot.txt`) built |
| D11 | Third-party tool data (Cooling Center, save tools) | own history first, import optional |
| D12 | Case export | local only (USB/QR), masked (account ID, serial number, MAC, IP, title names, SMB passwords), never network |
| D13 | Overall health score | **no**; traffic lights per domain with "insufficient data" |
| D14 | Display and audio tests | only if the UI demonstrably outputs sound and a 1:1 picture |
| D15 | Account and activation tools | detect, warn, back up save data; activate nothing (rule `savedata.foreign_account` is the first step) |
| D16 | SQLite in the payload | **integrated** (public domain, compiled without file access, only a copy in memory) |
| D17 | Contents of sensitive files | never read (messages, accounts, save data contents, trophies); databases only header and integrity on a copy; **exception (Marc, 11 Nov): `app.db`, only titleId and metaDataPath** |
| D18 | Router hints | router restart yes, router firmware never |
| D19 | Error codes from the history | **4 of 6 included** (`WV-109145-0`, `NW-102261-2`, `WV-109168-5`, `CE-100008-9`); `CE-105773-3` and `CE-118002-2` have no Sony page (HTTP 404) and stay open without evidence |
| D20 | Rule pack governance (two keys, rollback protection) | Rollback protection and format level **built**; second key only at a key change; expiry date no |
| D21 | Cleaning up own legacy files (`probe.txt`, `padprobe.*`, `ui.stdout`) | only move to `_to_delete/`, never delete |

## Licences and acknowledgements

- **Taken over as idea or code:** SQLite (public domain, `third_party/sqlite3/`); idea of the database check after PS5 Cleanup (da_Chris86, GPL-3.0-or-later). Entry in `THIRD_PARTY.md`.
- **Only ideas, no code and no data:** Console Service Tool (amoamare, no licence). Inspiration for a later hardware error code page in the PC companion; data would have to come from primary sources.
- **Can be taken over as code (licence checked, not yet integrated):** `driftcheck5` (GPL-3.0-or-later, drift test), Marc's own Cooling Center (GPL-3.0, README says "or later"; file headers missing). Sources with only the GPLv3 text and no version statement are treated as "version 3 only": there only the idea is re-implemented.
- All licence statements are checks of the repository, not legal advice.

## Excluded (will never be built)

- Any advice on updates of the system software or firmware (also of controller, TV, router), "reset/reinstall" as a step; Sony error codes with such steps are included only with update-free wording (check in `text_policy.cpp` and `test_error_codes.cpp`).
- Exploits, kernel write access, privilege escalation outside the tile launcher, decrypting and signing, creating backports, obtaining or distributing games or keys.
- Flash and NOR write access, writing fan or ICC values, `errlog clear`, raw UART commands, live UART in the console app.
- Deleting user data (only moving to `_to_delete/` with confirmation, backup and undo); reading messages, accounts, trophy and save data contents.
- Network to the outside without explicit approval (D1).
