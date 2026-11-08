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

### 4.2 Reparieren (MVP: 5 sichere Fixes)
| Fix-ID | Aktion | Reversibel durch |
|---|---|---|
| `rename_image_parentheses` | Klammern aus `.ffpfs`/`.ffpfsc`-Dateinamen entfernen | Rückbenennung aus Journal |
| `smp_set_key` | Einzelnen Key in `/data/shadowmount/config.ini` setzen (z. B. `kstuff_game_auto_toggle=0`) | Backup der config.ini |
| `smp_disable_conflicting_backpork` | BackPork-Autostart deaktivieren, wenn `backport_fakelib=1` | Backup Autoload-Liste |
| `move_duplicate_titleid` | Doppelte Quelle gleicher TITLE_ID nach `_duplicates/` verschieben | Zurückverschieben |
| `fix_image_nesting_hint` | Nur Hinweis + Anleitung (kein Auto-Fix, Images werden nicht verändert) | – |

### 4.3 Bericht
- Export als Text/Markdown nach `/data/ps5medic/reports/` und als **QR-Code** auf dem Bildschirm (Handy scannen → Bericht kopieren).
- Anonymisierung: IPs, Konto-IDs, Seriennummern werden entfernt.

### 4.4 Später (v2+)
- Live-Monitor: kstuff/SM+-Crash in Echtzeit erkennen und Ursache anzeigen.
- Regel-Update aus GitHub (manuell, SHA-256-geprüft).
- Vergleich „vorher/nachher“ zweier Check-ups.
- Mitmachen: Befunde ohne Regel („unbekannte Fehlerzeilen“) sammeln und als Issue-Vorlage exportieren.

## 5. Bedienung (Vollbild, Controller)
- **Start-Screen:** grosser Knopf „Check-up starten“, darunter letzte Ergebnisse.
- **Befundliste:** ✕ öffnet Details, △ Reparieren, □ Bericht, ◯ zurück.
- **Detail:** Evidenz, Erklärung, Quelle, Schritte. Reparieren zeigt **vorher** genau, was geändert wird.
- **Verlauf:** Liste aller Reparaturen mit „Rückgängig“.
- Sprache: Deutsch + Englisch (Strings in JSON).

## 6. Nicht-Ziele
- Kein Jailbreak, kein Payload-Loader-Ersatz, kein Game-Installer.
- Keine Spiele-, Key- oder Firmware-Funktionen.

## 7. Erfolgskriterien
- Check-up < 5 s auf der Konsole.
- Jede MVP-Regel hat mind. ein Fixture + Test.
- Kein Fix ohne Backup; Undo stellt den Ursprungszustand bitgenau her (Test).
