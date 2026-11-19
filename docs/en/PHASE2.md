# Phase 2 – Preparation and first console run

[Deutsch](../PHASE2.md) · **English**

As of 8 Nov 2026. The diagnostic payload ran on a console for the first time on the same day; the results are below in the section "Result of the first run". The table "Open points" describes the state **before** that run.

## What is already there
- `make ps5` builds `build-ps5/ps5medic.elf` (FreeBSD x86-64 ELF, ~2 MB) with the ps5-payload-sdk. The SDK is **not** in the repo: default path `~/ps5-sdk/PS5_PAYLOAD_SDK` (WSL), overridable with `PS5_PAYLOAD_SDK=...`. Source of the copy: `PS5 SDK usw\PS5_PAYLOAD_SDK` (GPLv3, John Törnblom). The wrapper scripts use the system clang/lld of the WSL distribution.
- `ps5/ps5_platform.cpp`: `Ps5Platform` with the real file system (`/`), process list via `sysctl kern.proc.proc`, firmware from `kernel_get_fw_version()`, model, memory via `statfs`.
- `ps5/main.cpp`: diagnostic payload. Collects, evaluates rules, writes findings to stdout and klog, plus `/data/ps5medic/last_check.txt` and a **snapshot** to `/data/ps5medic/snapshots/<time>/`. It repairs nothing.
- Snapshot export (`libmedic/src/snapshot_export.cpp`): writes the state in fixture format. Test: all 43 fixtures yield the same facts and findings after export and re-reading. `medic-cli export <folder> <target>` does the same on the PC.

## First run (you, at the console)
```bash
make ps5
make deploy PS5_HOST=<ip>      # FTP server (port 2121) must be running
```
1. `ps5medic.elf` then lies in `/data/homebrew/PS5MEDIC/`, the rules in `/data/ps5medic/knowledge/rules.json`.
2. Start the ELF via the Payload Manager (port 9021 is closed on Unified JB).
3. Fetch via FTP: `/data/ps5medic/last_check.txt` and the folder `/data/ps5medic/snapshots/<time>/`.
4. On the PC: `build/medic-cli check <snapshot-folder>`.

The snapshot contains file and folder names (e.g. game titles), but no game content, serial numbers or IPs. Look at it before sharing.

## Open points the first run is meant to clarify
| Question | Where visible | Consequence |
|---|---|---|
| Is the FW conversion (`0x04030000` → `4.03`) correct? | "Firmware:" line | otherwise adjust `fwver.hpp` |
| What are the processes really called (`etaHEN`, `kstuff`, `shadowmountplus`, `backpork`)? | "Processes:" list, `procs.json` | Rules A1/A3/B1/B5 compare names exactly |
| May the payload read `/data` and `/mnt/*`? | empty paths in the snapshot, errors in klog | otherwise set a root directory (pattern: SDK sample `list_files`) |
| Where do versions of kstuff, ShadowMount+, etaHEN come from? | `components` in `system.json` is empty | without a source B1, B3, B4 do not fire |
| Does `/mnt/shadowmnt/...` exist as described in the README? | snapshot under `fs/mnt/shadowmnt` | B6/B7 |
| Which autoload list does the console use? | snapshot contains `autoload.txt`, if present | B5 fix |

## Result of the first run (8 Nov 2026)
Console: `CFI-7021 B01Y` (probably a PS5 Pro), firmware 12.00. **`CLAUDE.md` names FW 4.03 as the test console; this console deviates from that.** The FW conversion is confirmed: ShadowMount+'s `debug.log` itself reports "FW: 12.00".

| Question | Result | Consequence in code |
|---|---|---|
| FW conversion | `12.00` is correct (SM+ reports the same) | no change |
| Process names | carry the suffix `.elf` (`kstuff.elf`, `shadowmountplus.elf`, `pldmgr.elf`, `elfldr.elf`, …); no etaHEN | collector strips `.elf`, otherwise A1/A3/B1/B4/B5 would never have fired |
| Read access | `/data` and `/mnt/usb0` readable, also `/data/shadowmount`, `/data/pldmgr` | none needed |
| `/mnt/shadowmnt` | exists, layout as in the README (`<name>_<hash>`, `pfsc`) | no change |
| Versions | `components` stays empty. The SM+ version is in `debug.log` (`ShadowMount+ v1.7beta11`) | collector reads `smp.version` from `debug.log` (latest start line). kstuff and etaHEN: still no source, B1/B4 do not fire there |
| Autoload list | `/data/pldmgr/autoload.txt`, entries as `local/<name>` without suffix | B5 fix matches by regex, fits |
| Findings | B2 (parentheses) and B8 (duplicate TITLE_ID) were real. **B10 was a false positive**: `sce_sys/npbind.dat` in a folder without `param.json` counted as a `.dat` image at great depth | collector skips `sce_sys`; fixture `b10_neg_sce_sys_dat` |

New regression fixtures (synthetic): `a1_pos_elfnames`, `b10_neg_sce_sys_dat`, `b3_pos_logversion`, `b3_neg_logversion`. The real snapshot does **not** go into the repo: it contains file names from the user's collection.

Practical notes:
- The payload returns its output over the same connection when you send it to port 9021 (`nc <ip> 9021 < build-ps5/ps5medic.elf`). On this console port 9021 listens, unlike what `CLAUDE.md` assumed.
- Fetch the snapshot: `wget -r -l inf -nH -np ftp://<ip>:2121/data/ps5medic/snapshots/<time>/` – without `-l inf` wget cuts off at depth 5 and the game folders (`fs/mnt/usb0/homebrew/<game>/sce_sys/…`) are missing.

Still open: a source for the kstuff version; whether `api_bind_address=0.0.0.0` in the SM+ config (API reachable on the LAN) deserves an advisory finding.

## UI decision (Phase 3)
`PS5_VulkanTemplate` is not available locally. Locally available are `ps5-native-app-boilerplate-main` and `ps5-opengl-sdk` (showcase PPSA99005). Whether Vulkan or OpenGL runs on FW 4.03 can only be tested on the console. Proposal: first evaluate the diagnostic run, then try a graphics sample.
