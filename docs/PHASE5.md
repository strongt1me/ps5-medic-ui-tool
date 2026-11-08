# Phase 5 – Community

Stand 8. Nov. 2026. Regel-Update, unbekannte Fehlerzeilen, Issue-Vorlage und Tester-Anleitung stehen. **Nicht gemacht** (bewusst, liegt beim Autor): Beiträge in Foren oder auf Discord, ein Download von Regeln aus dem Netz und das GitHub-Release.

## 1. Regel-Update (manuell, SHA-256)

`libmedic/src/rule_update.cpp`, in der UI unter Einstellungen → *Regel-Update*.

**Entscheidung: kein Download.** `CLAUDE.md` erlaubt als einzige Netzfunktion ein manuell ausgelöstes, SHA-256-geprüftes Regel-Update. Umgesetzt ist es als **Datei-Update ohne Netz**: Der Nutzer legt `rules.json` und `rules.json.sha256` selbst nach `/data/ps5medic/knowledge/update/` (FTP oder USB-Kopie). Gründe:
- Ein Download bräuchte TLS auf der Konsole und, damit die Prüfsumme überhaupt etwas schützt, einen vom Download getrennten Vertrauensanker (z. B. eine eingebaute Signatur). Eine Prüfsumme, die vom selben Server kommt wie die Datei, schützt nur vor Übertragungsfehlern.
- Regeln steuern Reparaturen (`fix`, `fix_params`). Eine untergeschobene Regeldatei wäre ein echter Angriffsweg; ohne Netzfunktion gibt es ihn nicht.
- „Kein Netz nach außen" bleibt damit ausnahmslos wahr (auch die Port-9021-Prüfung geht nur an `127.0.0.1`).

Ein späterer Download wäre möglich, braucht aber eine Signatur (Schlüsselverwaltung beim Autor) und ist hier nicht angelegt.

**Prüfung** (`check_rule_update`, schreibt nichts), in dieser Reihenfolge; jeder Fehler nennt eine Kennung, die die UI übersetzt:
1. Datei da? (`no_rules_file`) · höchstens 2 MiB (`too_large`) · lesbar (`unreadable`)
2. `rules.json.sha256` vorhanden und mit 64 Hex-Zeichen als erstem Wort, wie `sha256sum` es schreibt (`no_checksum`)
3. SHA-256 stimmt (`checksum_mismatch`)
4. Regel-Pruefer (`load_rules_from_string`) akzeptiert die Datei, auch das Schema (`invalid_rules`, mit Meldung)
5. `db_version` ist **neuer** als die laufende (`not_newer`; gleich oder älter wird abgelehnt)

**Installation** (`install_rule_update`, erst nach ✕): prüft erneut · sichert die bisherige `/data/ps5medic/knowledge/rules.json` nach `backups/rules_<Zeit>.json` und liest das Backup gegen · schreibt die neue Datei atomar und liest sie gegen (bei Abweichung wird die alte zurückgeschrieben) · legt die Update-Dateien nach `update/installed/<Zeit>/` (nie löschen) · schreibt eine Zeile in `knowledge/update.log`. Die laufende App lädt die neuen Regeln sofort; die bisherigen Befunde gelten nicht mehr, der Start zeigt „noch nicht geprüft".

**Rollback** (`rollback_rule_update`, △ dann ✕): stellt die gesicherte Datei Byte für Byte wieder her, die ersetzte bleibt als `backups/rules_rolledback_<Zeit>.json` erhalten. Gab es vorher keine Datei im Wissensordner (es galten die mitgelieferten Regeln), wird die installierte beiseite gelegt.

**Schreibgrenze:** Alles liegt unter `/data/ps5medic/knowledge/`; ein Test prüft, dass sich sonst nichts ändert. Ohne Schreibzugriff, mit falscher Prüfsumme oder ungültigem Zeitstempel bleibt der Dateibaum unverändert.

Wo die App Regeln sucht: zuerst `/data/ps5medic/knowledge/rules.json`, dann die mitgelieferte `/data/homebrew/PS5MEDIC/rules.json` (`make launch-ui` überschreibt die erste Datei mit dem Stand des Repos, das ist der Entwicklungsweg).

## 2. Unbekannte Fehlerzeilen und Issue-Vorlage

`unknown_error_lines` in `libmedic/src/report.cpp`. Der Bericht (□) bekommt einen Abschnitt **Unbekannte Fehlerzeilen**: Zeilen des ShadowMount+-Logs, die nach Fehlern aussehen, aber zu **keinem Befund** gehören.

Die Erkennung ist bewusst eng. Am echten Log der Test-Konsole (10 163 Zeilen) gemessen: ein grobes „error/fail/crash" hätte 85 Zeilen geliefert, fast alles Rauschen („game error monitoring", `error=0` bei erfolgreichen Unmounts, der Spieltitel „Jump-and-Run"). Deshalb zählen nur:
- Fehlerwörter `failed, failure, panic, fatal, cannot, can't, unable, denied, corrupt(ed), timed out, aborted, exception, invalid` (Groß-/Kleinschreibung egal),
- `error:`/`error=` mit Wert **ungleich 0** (auch `0x16`),
- das großgeschriebene Wort `ERROR`.

Zeilen, die schon als Evidenz eines Befunds im Bericht stehen, fallen weg. Gleichartige Zeilen bilden eine Gruppe: ohne Uhrzeit, Zahlen und Kennungen (Hex-Zeichenfolgen ab 6 Stellen mit einer Ziffer) als `#`. Angezeigt werden bis zu 10 Gruppen, häufigste zuerst, mit der ersten Zeile als Beispiel. Auf dem echten Log bleiben genau die zwei echten Zeilenarten übrig (`image source cache update failed` für die umbenannten „Dirt"-Images).

`.github/ISSUE_TEMPLATE/problem.md` (zweisprachig): Beschreibung, Erwartung, Setup, Bericht einfügen, unbekannte Zeilen. Der Hinweis dort verlangt, den Bericht vor dem Absenden zu lesen, weil Datei- und Ordnernamen enthalten sind.

## 3. Tester

`docs/TESTER.md`: was gesucht wird (Firmware-Matrix), Schritte, Datenschutz, was die Maintainer mit einem Snapshot tun (synthetisches Fixture, nie echte Namen), wie man das Regel-Update ausprobiert, dazu ein **Entwurf** für einen Forenbeitrag. Der Entwurf ist nicht veröffentlicht und hat einen Platzhalter für den Release-Link.

## Offen

- **Beiträge** auf GBAtemp/Discord: entfallen vorerst, das Projekt bleibt privat (Entscheidung vom 8. Nov. 2026). Der Entwurf in `docs/TESTER.md` bleibt als Vorlage.
- **GitHub-Release:** entfällt, Repository privat. Lizenz: GPL-3.0-or-later (siehe `docs/PHASE4.md`). Die Issue-Vorlage hat nur in einem Repository mit Issues einen Sinn.
- **Download von Regeln** mit Signatur, falls gewünscht (siehe oben).
- **Tester-Fixtures** entstehen erst, wenn es Berichte gibt.
