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

## Offen, braucht die Konsole (nicht belegt, nur Verdacht)
1. `hbldr.c`: `sys_set_budget(0)` (Syscall 0x23b) beim Neustart der Big App – der Autor nennt auf älteren Firmwares Kernel-Panic; passt zum Absturzmuster „UI läuft, Kachel startet neu“. Test: Syscall weglassen (UI nutzt absolute Pfade).
2. Tracer-Prozess wird mitten im ptrace-Tausch beendet (neue Launcher-Instanz) – Phase in einer Sperrdatei führen.
3. kevent/`NOTE_TRACK` kann ein fremdes Kind von SceSysCore erfassen – Titel vor `pt_attach` prüfen.
4. Wiederholversuche (3 × 20 s) können doppelte Starts auslösen.
5. hbldr hängt `/system_ex` beschreibbar ein und legt `FAKE00000` an, wenn es fehlt – das widerspricht „keine Schreibzugriffe auf /system_ex“ in CLAUDE.md (Ausnahme dokumentieren oder Weg entfernen; Entscheidung von Marc).
6. Kein Sperrmechanismus (`flock`) zwischen Launcher, UI und Payload.

## Offen, host-seitig machbar
- `posix_fs`: `list_dir` ohne Fehlerflag, `slurp` ohne Streamprüfung, `read_file` ohne Größengrenze (große Logs).
- `collect_*`: Lesefehler ergeben leere Fakten statt „unbekannt“ (Fehlerverlauf, Logzeilen, Prozessliste); „resolved“ wird ohne Prüfung der Coverage gemeldet.
- Rückgabewerte in `LocalService` (`append_history`, `save_check`, `set_language`, `reload_rules`) und `selftest.cpp` (Budget) werden ignoriert.
- Whitelist prüft nur den Pfadtext; Zwischenordner als Symlink nicht abgefangen (`realpath` der Elternordner).
- Aktive `rules.json` wird beim Laden nicht auf Signatur geprüft (Signatur nur bei Installation).
- Snapshot-Export: Ordner-/Dateinamen, `param.json`, Prozessliste unmaskiert (D12 sagt anderes).

## Firmware 4.00 bis 14.10
Belegt: Das SDK 0.45 deckt alle 48 Firmwares von 4.00 bis 14.10 im Startcode ab; alle 292 Importe von `ps5medicui.elf` und die der Payloads sind in 33 entschlüsselten Firmware-Dumps (4.00 bis 12.70) vorhanden; Pfad-Zeichenketten (`priv/vshlog`, `priv/mms/app.db` …) in der SceShellCore aller geprüften Firmwares. Nicht belegt: Verhalten auf jeder Firmware außer 12.00, Layouts (`kinfo_proc`, App-Info), Firmwares ohne Dump (4.02, 5.00, 7.00, 8.00, 9.20, 10.00, 11.20, 12.40, 12.60, 13.x, 14.x). FW 5.02 meldet über `kernel_get_fw_version()` 5.10.
