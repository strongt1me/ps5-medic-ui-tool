<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Prüfbericht 12. Nov. 2026 (Stabilität, Sicherheit, Kompatibilität)

Werkzeuge: ASAN/UBSAN-Build des ganzen Projekts (`medic-tests`), Fuzzer `tests/fuzz_image.cpp` (`medic-fuzz-image <Läufe> <Startwert>`), vier lesende Prüfläufe (Sicherheit, Ausfallszenarien, stille Fehler, Firmware-Kompatibilität). Ohne Konsole nicht prüfbar ist alles, was nur auf der Hardware läuft; das steht unten als „offen“.

## Behoben und durch Tests gedeckt (Host)
| Fund | Fix |
|---|---|
| Use-after-free in `collect_smp_api` (ASAN) | Treffer werden sofort in Strings kopiert |
| exFAT: Clusterzahl im Kopf größer als das Image (Kettenschleife bis 2^32 Schritte) | Clusterzahl wird auf die Imagegröße begrenzt (`imgread.c`) |
| sqlite-WAL: Seitenzahl aus der Datei vor der 64-MiB-Grenze angelegt | Grenze zuerst prüfen |
| Firmware-Rohwert `0x…` machte jedes Spiel zu „braucht Backport“ | unlesbare Firmware gilt als unbekannt |
| Ein Fehler in einem Collector beendete den ganzen Check-up | jeder Collector einzeln gefangen, Fakt `collector.failed`; die Startseite zeigt dann „Prüfung unvollständig“ (auch bei leerem Regelsatz), nie Grün |
| Verlauf, `update.log`, `config.ini` wurden bei Lesefehler durch eine Zeile ersetzt | vorhanden, aber nicht lesbar ⇒ nicht schreiben |
| Fix-Engine/Journal: vorhandene, aber unlesbare Datei galt als „neu“ (kein Backup); unlesbares Journal galt als „leer“ | Abbruch bzw. Fehler statt Überschreiben |
| Snapshot-Export: Benutzername, `pass`/`pwd`/`login`, `smb://user:pass@…` im Log nicht geschwärzt | erweitert, `debug.log` wird geschwärzt (Tests in `test_fixes.cpp`) |
| Launcher beendete per SIGKILL die Pid aus dem alten Log (nach Neustart evtl. kstuff/SM+/Lüfterregler) | entfernt, beendet wird nur der eigene Thread-Name |
| hbldr schloss die laufende App ohne erneute Prüfung (Spiel startet in den 2 s davor ⇒ wird beendet) | `hbldr_kill_guard`: Prüfung direkt vor dem Kill; Warten auf das Ende der App mit 15-s-Limit |

Fuzzer: 270 000 Läufe über vier Startwerte ohne Absturz. UBSAN meldet nur Linksverschiebungen negativer Werte in `tweetnacl.c` (Fremdcode, bekannt harmlos).

## Launcher-Änderungen vom 12. Nov. (Verdachtspunkte, auf der Konsole noch nicht getestet)
- **Phase-Datei** `/data/ps5medic/launcher.phase` (`launching`/`monitoring`/`failed`/`done`): eine neue Launcher-Instanz beendet die vorige nie, solange diese im ptrace-Tausch steckt; sie wartet bis zu 90 s und bricht sonst mit Meldung ab.
- **Höchstens zwei Startversuche**, und der zweite nur, wenn keine Big App läuft (kein doppelter Start durch einen hängenden LaunchApp-Aufruf).
- **Testhebel ohne Neubau:** liegt die (leere) Datei `/data/ps5medic/no_budget_syscall` auf der Konsole, lässt hbldr den Syscall `sys_set_budget(0)` weg. Test: Datei anlegen, UI starten, dann den Launcher erneut senden bzw. die Kachel drücken, während die UI läuft. Stürzt die Konsole dann nicht mehr ab, ist S1 bestätigt und der Syscall kann dauerhaft entfallen. Läuft die UI ohne ihn nicht (Zugriff auf `/data`), bleibt er an.
- Nicht umgesetzt: Titelprüfung des Kindprozesses vor `pt_attach` (S3), `flock` zwischen Launcher/UI/Payload (S8) – beides ohne Konsole nicht prüfbar.

## PS-Taste in der UI (12. Nov. 2026, Konsolentests)
Befund: In der UI (Big App FAKE00000) führt die PS-Taste nicht zum Homescreen, es ertönt nur ein Ton; in Spielen und anderen Apps geht sie. Kein Tastenereignis erreicht die UI (`ui.log`). **Nicht die Ursache** (je einzeln getestet): neue UI (SDK 0.45) gegen alte UI vom 9. Nov. (`dist/`), neuer gegen alter Launcher, Marker `no_budget_syscall`, Testhebel `raise_skip` (`uid` und `caps` nicht setzen). SDL ruft `sceSystemServiceHideSplashScreen` selbst auf. Auch beim Start über die Kachel (nicht nur per ELF-Versand) kommt nur der Ton. Offen: Verhalten anderer per hbldr/websrv gestarteter Apps. Vermutung: es liegt an der geborgten App FAKE00000 (PSNow-eboot); eine native App mit eigenem Titel hätte dieses Problem nicht. Ausweg bis dahin: Kreis beendet die UI (sie schließt ihre App selbst).
Testhebel im Launcher (nur Diagnose): `/data/ps5medic/no_budget_syscall` (leer) und `/data/ps5medic/raise_skip` (Text `uid` und/oder `caps`).

## Offen, braucht die Konsole (nicht belegt, nur Verdacht)
1. `hbldr.c`: `sys_set_budget(0)` (Syscall 0x23b) beim Neustart der Big App – der Autor nennt auf älteren Firmwares Kernel-Panic; passt zum Absturzmuster „UI läuft, Kachel startet neu“. Test: Syscall weglassen (UI nutzt absolute Pfade).
2. Tracer-Prozess wird mitten im ptrace-Tausch beendet (neue Launcher-Instanz) – Phase in einer Sperrdatei führen.
3. kevent/`NOTE_TRACK` kann ein fremdes Kind von SceSysCore erfassen – Titel vor `pt_attach` prüfen.
4. Wiederholversuche (3 × 20 s) können doppelte Starts auslösen.
5. hbldr hängt `/system_ex` beschreibbar ein und legt `FAKE00000` an, wenn es fehlt – das widerspricht „keine Schreibzugriffe auf /system_ex“ in CLAUDE.md (Ausnahme dokumentieren oder Weg entfernen; Entscheidung von Marc).
6. Kein Sperrmechanismus (`flock`) zwischen Launcher, UI und Payload.

## Zweite Runde (12. Nov. 2026): Lesefehler und Schreibwege, Host-Tests grün (4668 Prüfungen)
Behoben:
- `PosixFs::list_dir`: ein nicht lesbarer Eintrag beendet das Auflisten nicht mehr; `file_size` im Fehlerfall ergibt 0 statt 2^64-1; `slurp` prüft Lesefehler (`bad`) und begrenzt auf 256 MiB.
- Fehlerverlauf: fehlt der Ordner `/system_data/priv/error/history`, fehlt der Fakt (nicht geprüft) statt „0 Fehler"; ein nicht lesbares Log fehlt als Fakt statt „leeres Log"; eine vorhandene, nicht lesbare `config.ini` wird nicht als „fehlt" gemeldet; die ELF-Prüfung unterscheidet „unreadable" von „truncated".
- „Behoben/resolved" nur noch, wenn alle Fakten der Regel vorhanden sind (sonst wurde sie nicht geprüft).
- **Symlink als Zwischenordner:** `PosixRw` verweigert Schreiben, Anhängen, Umbenennen und Ordner anlegen, wenn ab der dritten Pfadstufe ein Ordner ein Symlink ist (`/data`, `/mnt/usbN` bleiben frei, weil sie Einhängepunkte sein können). Tests mit echtem Symlink.

Bewusst nicht geändert:
- Unlesbare `eboot.bin` eines Spiels ohne Backport-Ordner bleibt ein Befund „braucht Backport" (vorsichtig, ein Hinweis zum Nachprüfen); ein leerer Prozess-Snapshot (sysctl-Fehler) wird nicht gesondert behandelt, weil Regeln mit leerem Array ohnehin nicht feuern und Negativ-Regeln dafür Fixtures nutzen.
- Die UI-Verifikation nach einer Reparatur (`Verify::Fixed`) prüft die Vollständigkeit des Check-ups nicht (bräuchte einen eigenen Text in fünf Sprachen).

## Dritte Runde (12. Nov. 2026): Rückgabewerte, Selbsttest-Budget, Export-Maskierung, Host-Tests grün (4685 Prüfungen)
Behoben:
- `LocalService::run_checkup`: scheitert `append_history` oder `save_check`, steht `CheckResult.history_saved = false`; die Startseite zeigt dann statt der Regelversion „Der Verlauf konnte nicht gespeichert werden" (Schlüssel `start.history_failed`, fünf Sprachen). Vorher fehlte die Prüfung stillschweigend im Verlauf.
- Regel- und Fehlercode-Update (`install_*`, `rollback_*`): kann die neue Datei nicht geladen werden, bleibt das Ergebnis `ok`, aber `error = "reload_failed"`; die Update-Seite zeigt „gilt erst nach Neustart" (`update.reload_failed`). Vorher galten stillschweigend noch die alten Regeln.
- `set_language` liefert `bool`; die UI ignoriert das bewusst (die Sprache gilt dann für die laufende Sitzung).
- Selbsttest „Schreibtest" (`hw.storage.write_verify`): die Tagesmarke im Budget wird **vor** dem ersten Schreiben gesetzt. Lässt sich die Budgetdatei nicht schreiben, startet der Test nicht (`budget_unwritable`, übersprungen). Wird nichts geschrieben (sofortiger Abbruch/Fehler), wird die Marke wieder zurückgenommen; bricht die Konsole mitten im Test ab, läuft er am selben Tag nicht erneut.
- Snapshot-Export (D12): `redact_secrets` maskiert jetzt auch MAC-Adressen (`**:**:**:**:**:**`) und IPv4 (`x.x.x.x`, außer Loopback und `0.`; Versionsnummern und Firmware-Strings wie `12.00.00.00` bleiben) und Schlüssel mit `account`, `serial`, `email`, `psnid`. Gespiegelte `param.json`: `titleName`- und Konto-/Seriennummer-Felder werden `***`, `titleId` und Firmware-Felder bleiben; nicht lesbares JSON wird gar nicht kopiert. Tests für alles.

Bewusst nicht geändert:
- Signaturprüfung der aktiven `rules.json` beim Laden: nach der Installation liegt keine `.sig` mehr neben der Datei (sie wandert in `rules_update/done/`), und Datei-Updates (SHA-256) sowie von Hand kopierte Regeln haben gar keine. Eine Prüfung beim Laden würde diese gültigen Wege brechen. Die Datei liegt im eigenen Ordner `/data/ps5medic/knowledge/`; wer dort schreiben kann (FTP), hat ohnehin volle Kontrolle. Geschützt ist der Transportweg (Signatur Pflicht beim Netz-Download), und das Laden prüft Form, Regex-Güte und Textrichtlinie.
- Ordner- und Dateinamen im Export-Spiegel bleiben, weil die Diagnose am PC genau diese Namen auswertet (Payloads, Spielordner, Backport-Ordner); wer den Export weitergibt, sollte ihn vorher ansehen. Prozessnamen bleiben aus demselben Grund.

Weiterhin offen: nichts aus den drei Prüfrunden, was sich ohne Konsole beheben ließe; offen bleibt, was in „Offen, braucht die Konsole“ steht, sowie die zweite und dritte Runde auf der Konsole (zweite ist seit dem Check-up am 12. Nov. dort geprüft: Start, Check-up und Beenden ohne Fehler im Log, Reparatur nicht).

## Firmware 4.00 bis 14.10
Belegt: Das SDK 0.45 deckt alle 48 Firmwares von 4.00 bis 14.10 im Startcode ab; alle 292 Importe von `ps5medicui.elf` und die der Payloads sind in 33 entschlüsselten Firmware-Dumps (4.00 bis 12.70) vorhanden; Pfad-Zeichenketten (`priv/vshlog`, `priv/mms/app.db` …) in der SceShellCore aller geprüften Firmwares. Nicht belegt: Verhalten auf jeder Firmware außer 12.00, Layouts (`kinfo_proc`, App-Info), Firmwares ohne Dump (4.02, 5.00, 7.00, 8.00, 9.20, 10.00, 11.20, 12.40, 12.60, 13.x, 14.x). FW 5.02 meldet über `kernel_get_fw_version()` 5.10.
