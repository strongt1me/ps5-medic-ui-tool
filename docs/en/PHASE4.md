# Phase 4 – Report and release

[Deutsch](../PHASE4.md) · **English**

As of 8 Nov 2026. Report, anonymization, QR code, □ button, READMEs and `make dist` are in place and tested. **Not yet confirmed on the console:** whether the QR code can be read from the TV with a phone. **Decided (8 Nov 2026):** license GPL-3.0-or-later; the repository stays private, no public release.

## Report

`libmedic/src/report.cpp`, platform-independent (also runs on the PC: `medic-cli report <snapshot> [--lang de|en] [--out file] [--qr]`).

- **Full report** (Markdown): header with time, Medic version and rule base · environment (firmware, model, versions of kstuff/ShadowMount+/etaHEN/ps5-theme where known, running payloads from a fixed list of known names, storage per drive) · findings with severity, plain-language text, evidence, repair ID and source including status · repair history from the journal · note on anonymization.
- **Evidence with fact name** (`Evidence (free_pct): 1`): `Finding::evidence_path` comes from the rule; without it, values like "1" or "30" could not be interpreted.
- **Short version for the QR code**: pure ASCII text, at most **640 bytes** (`kQrMaxBytes`). With error correction M this yields at most QR version 20 (97 modules, 7 px per module on a 1080p TV) and stays readable from the sofa. Reports that are too long are truncated at finding boundaries ("… more findings in the report"); the path of the file is always at the end.
- **Storage**: `/data/ps5medic/reports/report_<timestamp>.md`. The path is built only from a valid timestamp (`report_path_for`), otherwise nothing is written.

## Write rule

The only write action besides repair and undo is □ in the findings list (`IMedicService::make_report`). Tests ensure: Square in the details or in the report writes nothing; with the real `LocalService` exactly one file is added under `/data/ps5medic/reports/`; without write access (PC preview) and with an invalid timestamp the file tree stays unchanged, but the QR text is still produced.

## Anonymization

`anonymize()` replaces in **everything** the report outputs (character scan, no `std::regex`):

| Pattern | Replacement |
|---|---|
| IPv4 (octets 0–255; versions like `1.10.2` or `v1.2.3.4` stay) | `[IP]` |
| IPv6 (with `::` or eight groups; times of day and C++ names stay) | `[IPV6]` |
| MAC (6 × 2 hex, `:` or `-`) | `[MAC]` |
| `/user/home/<8 hex>` | `/user/home/[ID]` |
| exactly 16 hex digits (with `0x` or at least one letter a–f; timestamps stay) | `[ACCOUNT-ID]` |
| `serial: …`, `seriennummer = …` | `[SERIAL]` |
| `account id: …`, `user_id=…`, `psn id: …` | `[ID]` |

Limits, deliberate: **file and folder names are kept**, since they are usually the evidence; the report says so and asks you to check before sharing. No collector gathers serial numbers; only what appears as "key: value" in evidence or log lines is masked. The placeholders are in square brackets because angle brackets disappear as HTML in Markdown.

## QR code

Encoder: [Nayuki's QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) (MIT) in `third_party/qrcodegen/`, unmodified, commit in `COMMIT.txt`. Thin wrapper in `libmedic/src/qr.cpp`. The UI encodes once when opening (not in every frame) and draws black on white with a quiet zone of 4 modules.

**Independently verified:** The rendered PNGs of the UI (`make preview`) were decoded with OpenCV (`QRCodeDetector`) and yielded exactly the short version (151 and 265 characters). Tests additionally check finder patterns, timing patterns, the fixed dark module and the behavior with text that is too long.

## First report run on the console (8 Nov 2026)

□ in the findings list, ◯ back: `ui.log` shows "Bildschirm: Bericht", the file `report_<time>.md` lay under `/data/ps5medic/reports/`, the QR code was scanned with a phone. Two defects in the real report were noticed, both fixed and tested:
- **Empty mount points** (`/mnt/usb1` … `/mnt/ext0` without a drive) reported the size of a tiny file system ("0.0 GB, 80 %") and filled the storage section. The report now lists only drives of 1 GB or more.
- **ShadowMount+ "version unknown"** although it is known: after hours the `debug.log` was longer than the last 2000 lines read; the start line with the version is only at the beginning. The collector now additionally checks the first 16 KiB (fixture `b3_pos_longlog`). Limit: if SM+ was restarted without log rotation, the version from the start of the log may be older than the running one.

## Release

- `make dist` builds `dist/ps5-medic-<version>/` (folder `PS5MEDIC` for `/data/homebrew/`, READMEs, `THIRD_PARTY.md`, `SHA256SUMS.txt`) and a zip. Local only, `dist/` is in `.gitignore`.
- README in German and English.
- Dropped: the "PPSA folder with `param.json`" from the original plan; the launcher entry (`ps5/homebrew/`) replaces it.

**Decisions of the author (8 Nov 2026):**
1. **License:** GPL-3.0-or-later (`LICENSE`, SPDX line in every own source file). The ELF links against the GPLv3 runtime libraries of the ps5-payload-sdk; the third-party components (MIT, zlib, BSD-2, DejaVu) are compatible with that (`THIRD_PARTY.md`).
2. **Public release (changed on 16 Nov 2026):** At first the repository `strongt1me/ps5-medic-ui-tool` stayed private (8 Nov, private pre-release `v0.4.0-dev.2` on 13 Nov). On 16 Nov 2026 Marc decided to make it public and to publish `v0.4.0` as the latest release. `make dist` builds the package; anyone who passes it on must offer the source code under the GPL.
3. The license texts of the third-party components in the package: `THIRD_PARTY.md` names them with their license; `make dist` includes `LICENSE`.
