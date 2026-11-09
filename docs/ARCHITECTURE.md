# PS5 Medic – Architektur

```
┌──────────────────────── PS5-App (Vollbild) ────────────────────────┐
│  UI (VulkanTemplate-Design)  ←→  App-Controller (Screens, Input)    │
└───────────────────────────────┬────────────────────────────────────┘
                                │
┌────────────────────── libmedic (plattformunabhängig) ──────────────┐
│  Collectors ──► Facts ──► RuleEngine ──► Findings ──► FixEngine    │
│      │                        ▲                          │         │
│      │                  rules.json                 Journal/Backup  │
└──────┼───────────────────────────────────────────────────┼─────────┘
       │                                                   │
  Platform-Interface (IFileSystem, IProcessList, ISystemInfo)
       │                                                   │
  ┌────┴─────────┐                               ┌─────────┴────────┐
  │ Ps5Platform   │  (SDK, sysctl, /data …)       │ SnapshotPlatform │ (PC: Fixture-Ordner)
  └──────────────┘                               └──────────────────┘
```

## Module
| Modul | Aufgabe |
|---|---|
| `platform/` | Interfaces + zwei Implementierungen: PS5 (echt) und Snapshot (Ordner auf dem PC, simuliert `/data`, Prozessliste als JSON). |
| `collectors/` | Je ein Collector pro Thema: `firmware`, `processes`, `hen`, `shadowmount`, `storage`, `payload_manager`, `plugins`, dazu (`collectors_stack.cpp`) Autoload-Listen, ELF-Dateien, nanoDNS-Log und etaHEN; Messwerte wie Temperatur und Dateisystemtyp je Laufwerk kommen aus der Plattform. Nur lesen! |
| `facts/` | Flacher Key/Value-Store, z. B. `fw.version=4.03`, `smp.config.kstuff_game_auto_toggle=1`, `proc.running[]`. |
| `rules/` | Lädt `rules.json`, wertet Bedingungen aus (Versionsvergleich, Regex auf Dateien/Logs, Existenz, Prozess läuft). |
| `fixes/` | Registrierte Fix-Aktionen mit `plan()` (zeigt Änderung), `apply()`, `undo()`. |
| `journal/` | `/data/ps5medic/journal.jsonl` – jede Änderung mit Hash vorher/nachher. |
| `report/` | Markdown-Bericht, Anonymisierung, QR-Code. |
| `app/` | PS5-Oberfläche. |
| `cli/` | `medic-cli check <snapshot-dir>` für PC/WSL. |

## Fakten-Quellen auf der PS5
| Fakt | Quelle (zu verifizieren in Phase 1) |
|---|---|
| Firmware | `sceKernelGetSystemSwVersion` bzw. sysctl aus dem SDK |
| Laufende Payloads | Prozessliste (sysctl `kern.proc`), Namen wie `kstuff`, `shadowmountplus`, `etaHEN` |
| ShadowMount+ | `/data/shadowmount/config.ini`, `debug.log`, `autotune.ini`, `manual.lst`, `manual.status`; API (Standard `127.0.0.1:10101`) |
| etaHEN | `/data/etaHEN/` (Plugins, Config) |
| Scan-Pfade/Images | `/data/homebrew`, `/data/etaHEN/games`, `/mnt/usb0..7`, `/mnt/ext0..1` |
| Speicher | `statfs` pro Mountpoint |

## Regel-Format
Siehe `knowledge/rules.schema.json`. Kurzform:
```json
{
  "id": "smp.parentheses_in_pfs_name",
  "severity": "critical",
  "when": { "all": [
    { "fact": "smp.images[].name", "matches": "\\(.*\\)", "ext_in": [".ffpfs", ".ffpfsc"] }
  ]},
  "text": { "de": "…", "en": "…" },
  "evidence": "smp.images[].path",
  "fix": "rename_image_parentheses",
  "source": "https://github.com/drakmor/ShadowMountPlus/issues/66",
  "verified": "community-report"
}
```

## Sicherheit
- App läuft mit erhöhten Rechten → jede Schreiboperation geht durch `FixEngine` (Whitelist erlaubter Pfade: `/data/shadowmount/`, `/data/ps5medic/`, Scan-Pfade, Payload-Manager-Config).
- Absturzsicher (Stromausfall, USB abgezogen): jede Operation steht vor der Ausführung als `intent` im Journal, das Journal wird nur angehängt (fsync), Dateien werden atomar ersetzt; `undo` rollt unvollständige Transaktionen anhand der Intents zurück und lässt sich wiederholen. Ein Plan, dessen Ausgangsdatei sich seit der Vorschau geändert hat, wird abgelehnt. Details: `docs/RULES.md`, „Sicherheitsprüfung der Fix-Engine“.
- Keine Shell-Ausführung, kein Laden fremder ELFs.
- Regel-Updates: nur JSON, Schema- und Hash-Prüfung, Rollback.
