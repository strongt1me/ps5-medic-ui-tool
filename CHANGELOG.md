# Changelog

## Unveröffentlicht / Unreleased

- **Deutsch:** Neues Design im Stil des Startbilds: Hex-Emblem, Systemstatus und Glow auf dem Startbild, Befundliste mit Detailfläche, Hex-Ergebniskarte nach einer Reparatur. Neu sind der Bereich **Reparaturen** (alle reparierbaren Befunde mit Vorschau und Backup) und der Bereich **Wartung** (Selbsttest, Regel-Update, Backups und Rückgängig, Bericht und Support-Paket).
- **English:** New design in the style of the splash screen: hex emblem, system status and glow on the start screen, findings list with a detail panel, hex result card after a repair. New are the **Repairs** area (all repairable findings with preview and backup) and the **Maintenance** area (self-test, rules update, backups and undo, report and support package).
- **Deutsch:** Die Reparatur-Vorschau (Notizen, Hinweise, Warnungen, Fehlermeldungen) und der Bericht (Markdown und QR-Kurzfassung) gibt es jetzt auch auf Französisch, Italienisch und Spanisch; vorher zeigten diese Sprachen dort Englisch. `medic-cli` am PC kennt weiterhin nur Deutsch und Englisch. Der Backup-Pfad in der Vorschau zeigt den Zeit-Platzhalter in der Sprache der Oberfläche, und vier Belegtexte sind sprachneutral.
- **English:** The repair preview (notes, hints, warnings, error messages) and the report (Markdown and QR short form) are now also available in French, Italian and Spanish; before, these languages showed English there. `medic-cli` on the PC still knows only German and English. The backup path in the preview shows the time placeholder in the interface language, and four evidence texts are language-neutral.

## 0.4.0 (2026-11-16): erste öffentliche Version / first public release

### Deutsch

**PS5 Medic** prüft auf einer gejailbreakten PS5 den Live-Zustand des Homebrew-Setups (Firmware, Jailbreak-Kette, kstuff, ShadowMount+, Payloads, Spiele, Dateien, Konfiguration), erklärt bekannte Fehlerursachen in Klartext und repariert, wo das sicher möglich ist, mit Backup und Rückgängig-Funktion. Eine native Vollbild-App für den Fernseher, bedient mit dem DualSense.

**Neu seit der letzten Vorabversion (0.4.0-dev.3)**

- **Fix-Vorschau mit Sicherungsanzeige:** Vor dem Bestätigen steht je Änderung, was gesichert wird (Kopie mit Größe, Verschieben ohne Kopie, Rechte), und wohin.
- **Vorher/Nachher-Vergleich:** Neben neu, behoben und unverändert zeigt er jetzt Schweregradwechsel und die geänderte Einrichtung (Firmware, Regelstand, ShadowMount+-, kstuff- und ampr-emu-ng-Versionen, Zahl der Spiele). Hat Medic einen Befund repariert, steht „durch Reparatur: …“ dabei (auch im Bericht).
- **Support-Paket** (△ im Bericht): ein ZIP mit Berichten, Fakten und Vergleich, in dem zusätzlich Spiel- und Titelnamen durch Platzhalter ersetzt sind. Den Schlüssel dazu behältst nur du.
- **Befunde ausblenden** (Options): Warnungen und Hinweise, die du nicht selbst beheben kannst, bleiben weg. Kritische lassen sich nicht ausblenden, bei höherem Schweregrad erscheint ein Befund wieder. Nur die Anzeige ändert sich, Verlauf und Berichte behalten alles.
- **Versionsabgleich von App und Dienst:** Nach einem Update bleibt kein alter Dienst im Speicher mehr, die App ersetzt ihn selbst.
- **Neue Regeln:** ampr-emu-ng 0.4.1 und kraken-mount 0.2.x, Meldungen aus `kraken.log`, Hinweise zu Laufzeit, Index, Trophäen-Dateien und unsignierten ELF-Dateien. Jetzt 101 Regeln. Der Rechte-Befund (S13) erklärt nur noch, er repariert nicht mehr.
- Die Version steht oben neben dem Titel, die Meldung „Dienstdatei nicht lesbar“ nennt ShadowMount+, die Spiele-Ansicht stimmt nach dem Ausblenden.
- **Qualität:** 7897 Prüfungen, ASan/UBSan und `clang-tidy` ohne Befund, Review der Neuerungen, Importe des Dienstes und der App in jeder lesbaren Firmware von 1.00 bis 12.70 vorhanden.

**Stand der Prüfung:** Die App unterstützt die Firmwares 4.00 bis 14.10 (soweit der Jailbreak dort läuft). Getestet wurde sie auf einer PS5 Pro (CFI-7021) und einer PS5 (CFI-2116), beide mit Firmware 12.00. Andere Firmwares und Setups sind nicht getestet; die Regeln sind einzeln als *bestätigt* oder *noch nicht bestätigt* gekennzeichnet.

**Installation:** `PS5-Medic-UI.zip` entpacken und die Ordner `PS5MEDIC` und `FAKE42007` per FTP nach `/data/homebrew/` kopieren, **oder** `ps5medic-launcher.elf` an den ELF-Lader (Port 9021) senden (er schreibt beide Ordner selbst, startet nichts und löscht nichts). ShadowMount+ muss laufen, es legt die Kachel an. Einzelheiten: [README](README.md).

Lizenz GPL-3.0-or-later; bei Weitergabe den Quelltext anbieten. Prüfsummen: `SHA256SUMS.txt` im Zip. PS5 Medic ist ein Community-Projekt und nicht mit Sony verbunden; die Nutzung geschieht auf eigene Verantwortung.

### English

**PS5 Medic** checks the live state of the homebrew setup on a jailbroken PS5 (firmware, jailbreak chain, kstuff, ShadowMount+, payloads, games, files, configuration), explains known causes of errors in plain language and, where it is safe, repairs them with backup and undo. A native full-screen app for the TV, operated with the DualSense.

**New since the last pre-release (0.4.0-dev.3)**

- **Repair preview with backup display:** before you confirm, each change shows what is backed up (copy with size, move without copy, permissions) and where.
- **Before/after comparison:** besides new, resolved and unchanged it now shows severity changes and the changed setup (firmware, rules version, ShadowMount+, kstuff and ampr-emu-ng versions, number of games). When Medic repaired a finding, “by repair: …” is shown (also in the report).
- **Support package** (△ in the report): a ZIP with reports, facts and comparison in which game and title names are also replaced by placeholders. Only you keep the key.
- **Hide findings** (Options): warnings and notices you cannot fix yourself stay out of the way. Critical ones cannot be hidden, and a finding comes back if its severity rises. Only the display changes; history and reports keep everything.
- **App and service version check:** after an update no old service stays in memory; the app replaces it itself.
- **New rules:** ampr-emu-ng 0.4.1 and kraken-mount 0.2.x, messages from `kraken.log`, hints on runtime, index, trophy files and unsigned ELF files. 101 rules now. The permissions finding (S13) now only explains and no longer repairs.
- The version is shown next to the title, the message “Service file not readable” names ShadowMount+, and the games view is correct after hiding.
- **Quality:** 7897 checks, ASan/UBSan and `clang-tidy` clean, review of the new work, the imports of the service and the app are present in every readable firmware from 1.00 to 12.70.

**State of testing:** the app supports firmware 4.00 to 14.10 (as far as the jailbreak runs there). It was tested on a PS5 Pro (CFI-7021) and a PS5 (CFI-2116), both on firmware 12.00. Other firmware versions and setups are untested; each rule is marked *confirmed* or *not yet confirmed*.

**Installation:** unzip `PS5-Medic-UI.zip` and copy the folders `PS5MEDIC` and `FAKE42007` to `/data/homebrew/` over FTP, **or** send `ps5medic-launcher.elf` to the ELF loader (port 9021) (it writes both folders itself, starts nothing and deletes nothing). ShadowMount+ must be running, it creates the tile. Details: [README](README.en.md).

Licence GPL-3.0-or-later; when passing it on, offer the source code. Checksums: `SHA256SUMS.txt` in the zip. PS5 Medic is a community project and not affiliated with Sony; use at your own risk.
