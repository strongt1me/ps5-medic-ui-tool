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
- **Fünf Sprachen:** Deutsch, English, Français, Italiano, Español (Einstellungen; die Wahl steht in `/data/ps5medic/config.ini`, Schlüssel `language`). Wissenstexte (Regeln, Fehlercodes, Selbsttests) für fr/it/es liegen in `knowledge/l10n/`; Berichte und die dynamischen Hinweise der Reparatur-Vorschau gibt es nur auf Deutsch und Englisch.
- **Pro Spiel:** Titelliste mit Quellen, Systemdatenbank, verlangter Firmware, Backport (auch im Spiel eingebaut), Dateiprüfung, Fehlern der Konsole, Spielständen und möglichen Gründen.
- Läuft auf den Firmwares 4.00 bis 14.10 (soweit der Jailbreak dort läuft); getestet ist bisher nur FW 12.00.

### 4.2 Reparieren (7 sichere Fixes)
| Fix-ID | Aktion | Reversibel durch |
|---|---|---|
| `rename_image_parentheses` | Klammern aus `.ffpfs`/`.ffpfsc`-Dateinamen entfernen | Rückbenennung aus Journal |
| `smp_set_key` | Einzelnen Key in `/data/shadowmount/config.ini` setzen (z. B. `kstuff_game_auto_toggle=0`) | Backup der config.ini |
| `smp_disable_conflicting_backpork` | BackPork-Autostart deaktivieren, wenn `backport_fakelib=1` | Backup Autoload-Liste |
| `move_duplicate_titleid` | Doppelte Quelle gleicher TITLE_ID nach `.duplicates/` im Scanpfad verschieben (Punkt-Ordner scannt SM+ nicht) | Zurückverschieben |
| `smp_translate_v1_keys` | Flache v1-Zeilen in einer v2-`config.ini` (die SM+ 1.7beta12 ignoriert) in die richtige Sektion übertragen | Backup der config.ini |
| `autoload_insert_delay` | `!4000` hinter elfldr in die Autoload-Liste einfügen | Backup der Autoload-Liste |
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
- **Fehlercodes:** Nachschlagewerk (Liste, Details mit Erklärung, Schritten und Quelle), nur lesen; siehe `docs/RULES.md`, „Fehlercodes nachschlagen“. Nennt ein Befund einen bekannten Code, springt □ im Befund-Detail zum Eintrag. Im Eintrag: □ Quelle als QR-Code, △ Code für den Bericht vermerken, nach einem Check-up „Was Medic gerade sieht“.
- Sprache: Deutsch + Englisch (Strings in JSON).

## 6. Nicht-Ziele
- Kein Jailbreak, kein Payload-Loader-Ersatz, kein Game-Installer.
- Keine Spiele-, Key- oder Firmware-Funktionen.

## 7. Erfolgskriterien
- Check-up < 5 s auf der Konsole.
- Jede MVP-Regel hat mind. ein Fixture + Test.
- Kein Fix ohne Backup; Undo stellt den Ursprungszustand bitgenau her (Test).
