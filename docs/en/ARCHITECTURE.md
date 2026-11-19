# PS5 Medic – Architecture

[Deutsch](../ARCHITECTURE.md) · **English**

```
┌──────────────────────── PS5 app (fullscreen) ──────────────────────┐
│  UI (own software canvas)  ←→  app controller (screens, input)      │
└───────────────────────────────┬────────────────────────────────────┘
                                │
┌────────────────────── libmedic (platform-independent) ─────────────┐
│  Collectors ──► Facts ──► RuleEngine ──► Findings ──► FixEngine    │
│      │                        ▲                          │         │
│      │                  rules.json                 Journal/Backup  │
└──────┼───────────────────────────────────────────────────┼─────────┘
       │                                                   │
  Platform interface (IFileSystem, IProcessList, ISystemInfo)
       │                                                   │
  ┌────┴─────────┐                               ┌─────────┴────────┐
  │ Ps5Platform   │  (SDK, sysctl, /data …)       │ SnapshotPlatform │ (PC: fixture folder)
  └──────────────┘                               └──────────────────┘
```

## Modules
| Module | Purpose |
|---|---|
| `platform/` | Interfaces + two implementations: PS5 (real) and snapshot (folder on the PC, simulates `/data`, process list as JSON). |
| `collectors/` | One collector per topic: `firmware`, `processes`, `hen`, `shadowmount`, `storage`, `payload_manager`, `plugins`, plus (`collectors_stack.cpp`) autoload lists, ELF files, nanoDNS log and etaHEN; measurements such as temperature and file system type per drive come from the platform. Read only! |
| `facts/` | Flat key/value store, e.g. `fw.version=4.03`, `smp.config.kstuff_game_auto_toggle=1`, `proc.running[]`. |
| `rules/` | Loads `rules.json`, evaluates conditions (version comparison, regex on files/logs, existence, process running). |
| `fixes/` | Registered fix actions with `plan()` (shows the change), `apply()`, `undo()`. |
| `journal/` | `/data/ps5medic/journal.jsonl` – every change with hash before/after. |
| `report/` | Markdown report, anonymization, QR code. |
| `app/` | PS5 user interface. |
| `cli/` | `medic-cli check <snapshot-dir>` for PC/WSL. |

## Fact sources on the PS5
| Fact | Source (to be verified in Phase 1) |
|---|---|
| Firmware | `sceKernelGetSystemSwVersion` or sysctl from the SDK |
| Running payloads | Process list (sysctl `kern.proc`), names like `kstuff`, `shadowmountplus`, `etaHEN` |
| ShadowMount+ | `/data/shadowmount/config.ini`, `debug.log`, `autotune.ini`, `manual.lst`, `manual.status`; API (default `127.0.0.1:10101`) |
| etaHEN | `/data/etaHEN/` (plugins, config) |
| Scan paths/images | `/data/homebrew`, `/data/etaHEN/games`, `/mnt/usb0..7`, `/mnt/ext0..1` |
| Storage | `statfs` per mount point |

## Rule format
See `knowledge/rules.schema.json`. Short form:
```json
{
  "id": "smp.parentheses_in_pfs_name",
  "severity": "info",
  "when": { "all": [
    { "fact": "smp.images[].name", "matches": "[()]" },
    { "fact": "smp.images[].ext", "in": [".ffpfs", ".ffpfsc"] }
  ]},
  "text": { "de": "…", "en": "…" },
  "evidence": "smp.images[].path",
  "fix": "rename_image_parentheses",
  "source": "https://github.com/drakmor/ShadowMountPlus/issues/66",
  "verified": "community"
}
```

## Security
- The app runs with elevated privileges → every write operation goes through the `FixEngine` (whitelist of allowed paths: `/data/shadowmount/`, `/data/ps5medic/`, scan paths, Payload Manager config).
- Crash-safe (power failure, USB unplugged): every operation is in the journal as an `intent` before execution, the journal is append-only (fsync), files are replaced atomically; `undo` rolls back incomplete transactions based on the intents and can be repeated. A plan whose source file has changed since the preview is rejected. Details: `docs/en/RULES.md`, "Safety review of the fix engine".
- No shell execution, no loading of foreign ELFs.
- Rule updates: JSON only, schema and hash check, rollback.
