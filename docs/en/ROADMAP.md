# PS5 Medic – Build Plan for Claude Code

[Deutsch](../ROADMAP.md) · **English**

Each phase ends with a working state and a short status report to Marc.

## Phase 0 – Host core (PC/WSL only, no console)
1. Repo skeleton: `libmedic/`, `cli/`, `app/`, `knowledge/`, `tests/fixtures/`, CMake, `Makefile`.
2. `IFileSystem`, `IProcessList`, `ISystemInfo` + `SnapshotPlatform` (reads `tests/fixtures/<case>/fs/…` and `procs.json`, `system.json`).
3. Fact store + collectors `shadowmount`, `processes`, `firmware`, `storage`.
4. Rule engine (load JSON, validate schema, conditions: `eq`, `lt/gt` with version comparison, `matches`, `exists`, `contains_line`).
5. Fixtures + tests for rules **B1–B15** and **A1, A3**.
6. `medic-cli check tests/fixtures/<case>` outputs findings as text/JSON.

**Done when:** `make test` is green, and every rule has a positive and a negative fixture.

## Phase 1 – Fix engine + journal
1. `plan/apply/undo` for the 5 MVP fixes.
2. Path whitelist, backups, `journal.jsonl` with SHA-256 before/after.
3. Tests: apply → undo → file tree identical.

## Phase 2 – PS5 platform (console FW 4.03)
> Status: done. Diagnostic payload built (`make ps5`) and run on the console on 8 Nov (FW 12.00, not 4.03 – see `docs/en/PHASE2.md`). `Ps5Platform` reads `/data` and `/mnt/*` freely, **without** an app jailbreak: payloads started through elfldr/Payload Manager are in principle not sandboxed; the jailbreak request was a wrong assumption (see `docs/en/PHASE3.md`, section "Correction"). Step 1 (graphics sample) is still pending.
1. **Check first:** Does a sample from PS5_VulkanTemplate run on FW 4.03? If not → SDL2 software rendering (plan B).
2. `Ps5Platform`: firmware, process list, file access – as a payload, without privilege escalation.
3. **Snapshot export on the console:** Medic can write its own state as a fixture folder to `/data/ps5medic/snapshots/` → fetch via FTP → test on the PC.

## Phase 3 – Fullscreen UI
> Status: The platform-independent part (canvas, font, de/en texts, nine screens, service interface, PNG preview) is in place and tested. PS5 Medic is built as a payload, not as a Sony title – no jailbreak request needed. `make ps5-ui` builds `ps5medicui.elf` with SDL2 (VideoOut/Pad) and `LocalService`. **Phase 3 is done:** On the console the UI runs when websrv's Big App loader starts it (`make launch-ui`; the pad only opens with an app context). The whole flow (check-up, repair, undo) was played through on the test console on 8 Nov – see `docs/en/PHASE3.md`.
1. ~~Generate the title with `new-title.py` from the template~~ dropped (no Sony title, see `docs/en/PHASE3.md`).
2. Screens: Start, Check-up running, Findings list, Detail, Repair (preview → confirm), History/Undo, Settings (language). – done.
3. Controller mapping per SPEC, strings in `i18n/de.json` and `en.json`. – done.

## Phase 4 – Report & release
> Status: Report, anonymization, QR code, □ button, READMEs and `make dist` are done and tested (see `docs/en/PHASE4.md`). License GPL-3.0-or-later; public since 16 Nov 2026 with release `v0.4.0` (private before that, decision of 8 Nov 2026).
1. Markdown report + anonymization + QR code. – done.
2. ~~`param.json`, icon, PPSA folder~~ dropped: launcher entry `ps5/homebrew/` (see `docs/en/PHASE3.md`). Installation via FTP to `/data/homebrew/PS5MEDIC/`.
3. README (DE/EN/FR/IT/ES, with images under `docs/images/`), `THIRD_PARTY.md` – done. GitHub release `v0.4.0` on 16 Nov 2026 on Marc's instruction (the earlier pre-releases `v0.4.0-dev.2` and `-dev.3` are removed beforehand).

## Phase 5 – Community
> Status: Rule update (file update and signed download), unknown error lines, issue template and tester guide are done and tested (see `docs/en/PHASE5.md`). Forum posts and GitHub release are dropped (repository private). The signed rule download is implemented (Ed25519, see `docs/en/PHASE5.md`).
1. Rule update (manual, SHA-256) and signed download (Ed25519). – done (put the file on the console or load it with □ from your own address; check and install in PS5 Medic, details in `docs/en/PHASE5.md`).
2. Collect "Unknown error lines" → issue template. – done (section in the report, `.github/ISSUE_TEMPLATE/problem.md`).
3. Find testers with FW 1.00–14.22 (GBAtemp, Discord), collect fixtures. – Guide and draft of the call in `docs/en/TESTER.md`; forum posts only on Marc's instruction; testers report via the issues in the public repository.

## First prompt for Claude Code
> Read CLAUDE.md and docs/*. Start with Phase 0, steps 1–3. Create one positive and one negative fixture each for rules B2 and B8, then show me the output of `medic-cli check` for all fixtures.
