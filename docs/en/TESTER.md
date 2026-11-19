# Taking part as a tester

[Deutsch](../TESTER.md) · **English**

So far PS5 Medic has been tested on **one** console (PS5 Pro, FW 12.00). The rules come from issues, READMEs and release notes and are only really verified where the catalogue `docs/en/RULES.md` says so. What is missing are other firmwares and setups.

## What we are looking for

| What | Why |
|---|---|
| **Firmware 4.00 to 13.60+** (the app is meant to run there), including those in between | Rules are tied to versions (e.g. A1 from 13.60, B3 only 13.60, B4 only 9.00); whether they fit other firmwares or fire wrongly, nobody knows yet. |
| **Other jailbreak chains**: etaHEN, kstuff-lite, Payload Manager, BD-JB, WebKit autoloader | Process names and start times differ (rules A1–A4). |
| **Different ShadowMount+ versions** | Config format and log lines change from version to version. |
| **Consoles with real problems** | Every case PS5 Medic does not recognise is a possible new rule. Especially valuable: the "Unknown error lines" in the report. |

A tester does not need to repair anything. A check-up and the report are enough.

## How it works

1. Download the release and copy the two folders `PS5MEDIC` and `FAKE42007` to `/data/homebrew/` via FTP (see README).
   Or without FTP: send `ps5medic-launcher.elf` (a separate file in the release, not in the zip) to the ELF loader (port 9021); it writes both folders itself.
2. Within seconds ShadowMount+ creates the **PS5 Medic** tile under "Media". Pressing it starts the app; on start it sends its service (`ps5medicservice.elf`) to the ELF loader itself, which has to be running for that (port 9021). Neither websrv nor a launcher is needed.
3. **✕** on "Start check-up".
4. **□** in the findings list: saves the report and shows the QR code. The file is in `/data/ps5medic/reports/`.
5. Paste the report into an issue (template: *Report a problem*). It is anonymised. **Read it first**: file and folder names (including game names) are in it.

Even if **no findings** come up, the report helps: it shows that a firmware and a setup ran through cleanly.

### If the tile does not start

Check that the ELF loader (port 9021) is running and that both folders are complete in `/data/homebrew/`. As a fallback, only the diagnostic payload `ps5medic.elf` can be started (via Payload Manager or to port 9021). It is **read-only** and writes `/data/ps5medic/last_check.txt` and a snapshot to `/data/ps5medic/snapshots/<time>/`. Both can be fetched via FTP.

## Privacy

- PS5 Medic **sends nothing**. Network functions: the optional rule download (only with an address in `config.ini`, only on confirmation, only two GET requests) and, in the self-test, a TCP connection attempt to the router in your own network (no data, never the internet); reports and snapshots leave the console only if you pass them on yourself.
- The **report** replaces IP and MAC addresses, user and account IDs and serial numbers. **File names and paths stay**, because they are usually the evidence.
- The **snapshot** contains the names of files and folders under the ShadowMount+ scan paths, so also game names, and the `debug.log` of ShadowMount+. Only pass it on if you are fine with that.
- Do not share anything that contains keys, firmware or game contents. PS5 Medic does not export anything like that.

## What the maintainers do with a report or snapshot

1. Use `medic-cli check <snapshot>` or `medic-cli report <snapshot>` on the PC to reproduce what PS5 Medic sees.
2. Build a **synthetic** fixture under `tests/fixtures/` from it: the structure and the features the rule needs stay (extensions, brackets in the name, title IDs, configuration lines), real file and game names are replaced. **Real names never go into the repo.**
3. Add a new or corrected rule in `knowledge/rules.json` with a source, plus a positive and a negative fixture (the tests enforce both), and extend `docs/en/RULES.md`.
4. The rule appears in the next rule update.

## Trying out a rule update

PS5 Medic **never downloads anything on its own**. A rule update is either a file that you put on the console yourself, or a download that you explicitly trigger (below). The file first:

1. Get the new `rules.json` from the release and create the checksum next to it: `sha256sum rules.json > rules.json.sha256`.
2. Put both files into `/data/ps5medic/knowledge/update/` via FTP or USB copy.
3. In PS5 Medic: More, **Update rules**. The screen shows the version found and whether the SHA-256 checksum matches. **✕** installs; the existing file is backed up first (`/data/ps5medic/knowledge/backups/`), nothing is deleted. With **△** the last installation can be rolled back.

**Download:** in the **Rule update** screen press **□** (without an entry in `config.ini`, PS5 Medic fetches from GitHub, `https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/`; you enter another source as the line `update_url=http://<address>/` or `https://…` in `/data/ps5medic/config.ini`), and confirm the prompt with **✕**. PS5 Medic fetches `rules.json` and `rules.json.sig`, checks the Ed25519 signature against the built-in key and only then offers the file; installing again happens only with **✕**. Without a valid signature the file is rejected, even if a checksum matches. `https://` runs over the built-in TLS (BearSSL; the console's date and time must be correct), `http://` without TLS; in both cases the signature secures authenticity. No redirects.

The checksum protects against damaged or truncated copies. It does **not** protect against a deliberately substituted file with a matching checksum; anyone who can put files on your console can do more than that anyway. Only take rule files from the project's release.

## Draft for a call in forums or on Discord (not published, stays a template; forum posts only on Marc's instruction)

> **PS5 Medic is looking for testers (FW 4.00 to 13.60+)**
>
> PS5 Medic is a full-screen app for jailbroken PS5 consoles that checks the homebrew setup (ShadowMount+, kstuff, payloads, file names, configuration), explains errors in plain language and repairs safe cases with backup and undo. So far tested only on a PS5 Pro with FW 12.00.
>
> Wanted: other firmwares and setups. You do not have to repair anything: start the check-up, press □, paste the anonymised report into an issue. Even "no findings" is a useful result. PS5 Medic sends nothing and only downloads rules (with a signature) on your command from an address you enter yourself.
>
> Link to the release and to the issue template: https://github.com/strongt1me/ps5-medic-ui-tool/releases/latest and https://github.com/strongt1me/ps5-medic-ui-tool/issues/new/choose
