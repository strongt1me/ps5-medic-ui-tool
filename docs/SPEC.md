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

### 4.2b Fehlercodes nachschlagen
- Eigene Karte auf dem Start-Screen („Fehlercodes“): alle **155 PS5-Fehlercodes** der offiziellen Sony-Liste (Stand 9. Nov. 2026), nach Familie (CE, NP, WS, NW, WV, SU, E2) geordnet. Rechts zu jedem Code: Bedeutung, Schritte zur Lösung, ein **Homebrew-Hinweis** für gejailbreakte Konsolen und der Link zur offiziellen Seite.
- Reines Wissen aus `knowledge/error_codes.json` (in die UI eingebettet, `tools/embed_error_codes.py`): liest und schreibt nichts auf der Konsole. △ startet den Check-up, wenn die Ursache auch in der Homebrew-Umgebung liegen kann.
- Am PC: `medic-cli error CE-108255-1` (Schreibweisen egal, auch Teilsuche wie `108255`), `medic-cli error --list [familie]`.
- Texte und Schritte sind eigene Zusammenfassungen der Sony-Seiten, kein Sony-Wortlaut; die offizielle Seite bleibt die Quelle. Sperren (WS-116331-5 u. ä.) gehen von Sony aus und werden weder behoben noch umgangen.

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
- **Fehlercodes:** ←→ Familie, ↑↓ Code, L1/R1 Seite, △ Check-up (wo er helfen kann), ◯ zurück.
- Sprache: Deutsch + Englisch (Strings in JSON).

## 6. Nicht-Ziele
- Kein Jailbreak, kein Payload-Loader-Ersatz, kein Game-Installer.
- Keine Spiele-, Key- oder Firmware-Funktionen.

## 7. Erfolgskriterien
- Check-up < 5 s auf der Konsole.
- Jede MVP-Regel hat mind. ein Fixture + Test.
- Kein Fix ohne Backup; Undo stellt den Ursprungszustand bitgenau her (Test).
