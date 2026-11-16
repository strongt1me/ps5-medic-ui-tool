# PS5 Medic – Spezifikation

## 1. Problem
Jailbreak-Setups auf der PS5 bestehen aus vielen Teilen (Exploit-Kette, HEN/kstuff, ShadowMount+, Payload-Manager, Plugins), die versions- und firmwareabhängig zusammenspielen. Typische Fehler (Kernel-Panic, Spiel startet nicht, Image wird nicht gemountet) landen als immer gleiche Hilferufe in Foren und GitHub-Issues. Die Ursache steht oft in einer Config-Zeile, einem Dateinamen oder einer Versionskombination.

## 2. Ziel
Ein Knopfdruck auf der PS5 → „Check-up“ → Liste der Befunde mit Ampel, Klartext-Erklärung, Quelle und – wo möglich – „Reparieren“-Knopf.

## 3. Zielgruppe
- Einsteiger, die nur „es geht nicht“ wissen.
- Fortgeschrittene, die einen sauberen Bericht für Foren/Issues brauchen.

## 4. Funktionen

### 4.1 Check-up (MVP)
- Erkennt: Firmware, Konsolen-Modell, laufende Payloads/Prozesse, HEN (etaHEN / kstuff-lite / HyperHEN / OnionHEN), Versionen, ShadowMount+ (Version, `config.ini`, `debug.log`, `manual.lst`), Speicherplätze, Scan-Pfade.
- Wertet Regeln aus `knowledge/rules.json` aus.
- Zeigt Befunde mit Schweregrad: 🔴 kritisch · 🟠 Warnung · 🔵 Hinweis.
- Jeder Befund: Titel, Klartext, gefundene Evidenz (Zeile/Datei), betroffene Versionen, Quelle (Link), empfohlener Schritt.

### 4.1a Oberfläche (Stand 12. Nov. 2026)
- Startbild beim Start (`ui/assets/splash.jpg`, in die Programmdatei eingebettet; jede Taste überspringt es), dann der Startbildschirm.
- **Fünf Sprachen:** Deutsch, English, Français, Italiano, Español (Einstellungen; die Wahl steht in `/data/ps5medic/config.ini`, Schlüssel `language`). Ohne gespeicherte Wahl übernimmt die App beim Start die Systemsprache der Konsole (Deutsch, Englisch, Französisch, Italienisch, Spanisch); jede andere Systemsprache und jeder Lesefehler ergibt Englisch (Marc, 12. Nov. 2026). Wissenstexte (Regeln, Fehlercodes, Selbsttests) für fr/it/es liegen in `knowledge/l10n/`; Berichte und die dynamischen Texte der Reparatur-Vorschau (Hinweise, Notizen, Warnungen, Fehlermeldungen) gibt es nur auf Deutsch und Englisch (fr/it/es zeigen Englisch).
- **Pro Spiel:** Titelliste mit Quellen, Systemdatenbank, verlangter Firmware, Backport (auch im Spiel eingebaut), Dateiprüfung, Fehlern der Konsole, Spielständen und möglichen Gründen.
- Läuft auf den Firmwares 4.00 bis 14.10 (soweit der Jailbreak dort läuft); getestet ist bisher nur FW 12.00.

### 4.2 Reparieren (15 Fixes und 1 Hinweis)
| Fix-ID | Aktion | Reversibel durch |
|---|---|---|
| `rename_image_parentheses` | Klammern aus `.ffpfs`/`.ffpfsc`-Dateinamen entfernen | Rückbenennung aus Journal |
| `rename_case_prx` | `Il2cppUserAssemblies.prx` → `Il2CppUserAssemblies.prx` (nur auf der internen SSD, wo die Schreibweise zählt) | Rückbenennung aus Journal |
| `smp_set_key` | Einzelnen Key in `/data/shadowmount/config.ini` setzen (z. B. `kstuff_game_auto_toggle=0`) | Backup der config.ini |
| `smp_disable_conflicting_backpork` | BackPork-Autostart deaktivieren, wenn `backport_fakelib=1` | Backup Autoload-Liste |
| `move_duplicate_titleid` | Doppelte Quelle gleicher TITLE_ID nach `.duplicates/` im Scanpfad verschieben (Punkt-Ordner scannt SM+ nicht) | Zurückverschieben |
| `smp_translate_v1_keys` | Flache v1-Zeilen in einer v2-`config.ini` (die SM+ 1.7beta12 ignoriert) in die richtige Sektion übertragen | Backup der config.ini |
| `autoload_insert_delay` | `!4000` hinter elfldr in die Autoload-Liste einfügen | Backup der Autoload-Liste |
| `autoload_remove_duplicates` | Doppelte Zeilen einer Autoload-Liste entfernen (erste bleibt) | Backup der Autoload-Liste |
| `autoload_fix_missing` | Autoload-Eintrag ohne Datei: Schreibweise korrigieren, sonst auskommentieren (WebKit-Liste) bzw. entfernen (Payload Manager) | Backup der Autoload-Liste |
| `pldmgr_enable_autoload` | `AUTOLOAD_ENABLED=1` in `/data/pldmgr/pldmgr_config.txt` setzen | Backup der Datei |
| `quarantine_broken_elf` | Unbrauchbare ELF-Datei nach `.medic_broken/` daneben verschieben (nur Whitelist-Ordner, nie löschen) | Zurückverschieben |
| `quarantine_empty_image` | Leeres oder abgebrochenes Image (unter 512 Byte) nach `.medic_broken/` daneben verschieben (nur Whitelist-Ordner, nie löschen) | Zurückverschieben |
| `quarantine_leftovers` | Reste abgebrochener Kopier-/Packvorgänge (`.part`, `.tmp`, `.teil` ... direkt in den Scanpfaden) nach `.medic_broken/` im selben Ordner verschieben (nie löschen; Größenprüfung, falls die Datei noch wächst) | Zurückverschieben |
| `quarantine_partial_folder` | Halbfertigen Packordner (zstd-Volumes ohne `ampr_zstd.index`) direkt in einem Scanpfad als ganzen Ordner nach `.medic_broken/` im selben Scanpfad verschieben (nie löschen) | Zurückverschieben |
| `chmod_program_files` | `eboot.bin` und Module ohne Ausführungsrecht (`CE-107750-0`) auf 0777 setzen; das Journal merkt sich den alten Modus jeder Datei | Alte Rechte wiederherstellen |
| `fix_image_nesting_hint` | Nur Hinweis + Anleitung (kein Auto-Fix, Images werden nicht verändert) | – |

### 4.3 Bericht
- Export als Text/Markdown nach `/data/ps5medic/reports/` und als **QR-Code** auf dem Bildschirm (Handy scannen → Bericht kopieren).
- Anonymisierung: IPs, Konto-IDs, Seriennummern werden entfernt.

### 4.4 Später (v2+)
- Live-Monitor: kstuff/SM+-Crash in Echtzeit erkennen und Ursache anzeigen.
- ~~Regel-Update (manuell, signiert)~~ und ~~Vergleich „vorher/nachher“ zweier Check-ups~~ sind seit Phase 5 fertig (`docs/PHASE5.md`).
- Mitmachen: Befunde ohne Regel („unbekannte Fehlerzeilen“) sammeln und als Issue-Vorlage exportieren.

## 5. Bedienung (Vollbild, Controller)
- **Start-Screen:** grosser Knopf „Check-up starten“, darunter letzte Ergebnisse.
- **Befundliste:** ✕ öffnet Details, △ Reparieren, □ Bericht, ◯ zurück.
- **Detail:** Evidenz, Erklärung, Quelle, Schritte. Reparieren zeigt **vorher** genau, was geändert wird.
- **Verlauf:** Liste aller Reparaturen mit „Rückgängig“.
- **Fehlercodes:** Nachschlagewerk (Liste, Details mit Erklärung, Schritten und Quelle), nur lesen; siehe `docs/RULES.md`, „Fehlercodes nachschlagen“. Nennt ein Befund einen bekannten Code, springt □ im Befund-Detail zum Eintrag. Im Eintrag: □ Quelle als QR-Code, △ Code für den Bericht vermerken, nach einem Check-up „Was Medic gerade sieht“.
- Sprache: Deutsch, Englisch, Französisch, Italienisch, Spanisch (Strings in JSON, `ui/i18n/`, Wissenstexte in `knowledge/l10n/`).

## 6. Nicht-Ziele
- Kein Jailbreak, kein Payload-Loader-Ersatz, kein Game-Installer.
- Keine Spiele-, Key- oder Firmware-Funktionen.

## 7. Erfolgskriterien
- Check-up < 5 s auf der Konsole.
- Jede MVP-Regel hat mind. ein Fixture + Test.
- Kein Fix ohne Backup; Undo stellt den Ursprungszustand bitgenau her (Test).
