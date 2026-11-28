# Phase 5 – Community

**Deutsch** · [English](en/PHASE5.md)

Stand 8. Nov. 2026. Regel-Update (Datei und signierter Download), unbekannte Fehlerzeilen, Issue-Vorlage und Tester-Anleitung stehen. **Entschieden (8. Nov.):** privat, kein GitHub-Release, keine Foren-Beiträge; **geändert am 16. Nov. 2026:** Das Repository wird öffentlich, `v0.4.0` ist das Latest-Release (siehe `docs/PHASE4.md`).

## 1. Regel-Update (Datei oder signierter Download)

`libmedic/src/rule_update.cpp` (prüfen, installieren, zurücksetzen), `rule_download.cpp` und `http.cpp`/`posix_http.cpp` (Download), `signature.cpp`/`ed25519.cpp` (Signatur); in der UI unter Mehr → *Regeln aktualisieren*.

**Zwei Wege in denselben Ordner** `/data/ps5medic/knowledge/update/`:
- **Datei-Update (ohne Netz):** Der Nutzer legt `rules.json` und `rules.json.sha256` selbst dorthin (FTP, USB-Kopie). Die Prüfsumme schützt vor beschädigten Kopien, nicht vor einer absichtlich untergeschobenen Datei samt Prüfsumme; wer Dateien auf die Konsole legen kann, kann ohnehin mehr.
- **Download (Entscheidung von Marc, 8. Nov. 2026):** □ im Bildschirm *Regel-Update*, Rückfrage, ✕. PS5 Medic holt `rules.json` und `rules.json.sig` von `update_url` und legt sie in den Update-Ordner. **Angenommen wird nur mit gültiger Signatur.**

**Warum eine Signatur und nicht nur die Prüfsumme:** Eine Prüfsumme, die vom selben Server kommt wie die Datei, schützt nur vor Übertragungsfehlern. Regeln steuern Reparaturen (`fix`, `fix_params`); eine untergeschobene Regeldatei wäre ein echter Angriffsweg. Deshalb signiert der Autor die Regeldatei mit Ed25519, der öffentliche Schlüssel ist in PS5 Medic eingebaut (`libmedic/src/trusted_keys.cpp`), der Privatschlüssel liegt nie im Repository.

**Was die Netzfunktion tut und nicht tut** (`CLAUDE.md`, Leitlinie 5):
- nur auf ausdrücklichen Befehl (□, dann ✕); nie beim Start, nie im Hintergrund
- nur zur Adresse, die der Nutzer selbst in `/data/ps5medic/config.ini` einträgt (`update_url=http://host:port/pfad/` oder `https://…`), **sonst zur Standardadresse** `https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/` (`kDefaultUpdateUrl`, Entscheidung von Marc, 17. Nov. 2026): die signierten Dateien im öffentlichen Repository
- zwei GET-Abrufe (`<url>/rules.json`, `<url>/rules.json.sig`), danach höchstens zwei weitere für die optionalen Fehlercodes (`<url>/error_codes.json`, `<url>/error_codes.json.sig`; 404 bei der Datei selbst heißt: der Server bietet sie nicht an, das ist kein Fehler) mit den Headern `Host`, `User-Agent: PS5-Medic`, `Accept`, `Connection: close`; keine Cookies, keine Zugangsdaten, nichts über Konsole oder Nutzer (ein Test prüft die Anfrage Byte für Byte)
- `http://` über BSD-Sockets (wie bisher, `posix_http.cpp`) und `https://` mit **BearSSL** (`third_party/bearssl`, MIT) über dieselben Sockets, am PC und auf der Konsole gleich (`ps5/ps5_http.cpp` reicht nur den Socket-Client weiter). Die Wurzelzertifikate sind eingebaut (`libmedic/src/tls_roots.c`: ISRG Root X1/X2, DigiCert Global Root G2, USERTrust RSA/ECC; GitHub liefert 2026 Zertifikate von Let’s Encrypt, Kette YR1 → Root YR → ISRG Root X1; erzeugt mit `brssl ta`). Geprüft werden Kette, Gültigkeit (Datum und Uhrzeit der Konsole müssen stimmen) und Servername, nur TLS 1.2. Die Echtheit der Regeln sichert unabhängig vom Kanal die Signatur der Datei. Fehler stehen als Kennung `tls` mit „Technische Angabe: BearSSL 62 (Zertifikat nicht vertrauenswürdig)“ im Bildschirm. **Warum nicht das TLS des Systems (`libSceHttp2`/`libSceSsl`):** die Konsole lehnte die Kette von GitHub mit `0x8095f00c` ab, und ohne Header der Systembibliothek ließ sich das nicht einstellen (ein Aufruf mit geratenem Flag hängte den Prozess; 18. Nov. 2026). Läuft ein eingebautes Zertifikat ab oder wechselt GitHub die Kette, muss `tls_roots.c` erneuert werden. Ein eigener kleiner Server im LAN geht weiter (`make serve-rules`)
- keine Weiterleitungen, höchstens 2 MiB für die Regeln und 4 KiB für die Signatur, 15 s je Abruf
- schreibt nur unter `/data/ps5medic/knowledge/`; bereits dort liegende Update-Dateien wandern nach `update/replaced/<Zeit>/`; `update.log` bekommt eine Zeile `rules_download`
- der Download prüft und installiert nichts; beides folgt der bekannten Kette (Prüfung, Angebot, Bestätigung, Backup, Undo)

**Signatur und Schlüssel**
- Format `rules.json.sig`: eine Zeile `ed25519 <schlüssel-id> <base64 der 64 Bytes>`. Die ID sind die ersten 16 Hex-Zeichen von SHA-256 über den öffentlichen Schlüssel und wählt den Schlüssel aus der eingebauten Liste. Signiert werden exakt die Bytes von `rules.json`.
- Eine daneben liegende Prüfsumme zählt dann nicht mehr: Liegt eine `.sig` im Ordner, entscheidet sie allein (kein Herabstufen auf die Prüfsumme). Ein Test deckt das ab.
- Rollback-Schutz: Auch eine gültig signierte, aber ältere Datei wird abgelehnt (`not_newer`). Ein Server kann Updates zurückhalten oder eine alte signierte Version ausliefern (wirkungslos), aber keine neue Regeldatei erfinden.
- Ed25519 steckt in `third_party/tweetnacl` (TweetNaCl 20140427, Public Domain, unverändert, als C übersetzt). Geprüft mit den RFC-8032-Testvektoren und, unabhängig davon, mit OpenSSL an der echten Signatur der ausgelieferten Regeln. TweetNaCl prüft in dieser Version nicht, ob der Skalar S der Signatur kanonisch ist; das erlaubt höchstens eine zweite gültige Signatur für **dieselbe** Datei und ist hier unerheblich.
- **Privatschlüssel:** `~/.ps5medic/rules-signing.key` (WSL, Rechte 600), erzeugt mit `build/medic-sign keygen`; eine zweite Kopie liegt unter `C:\Users\stron\.ps5medic\` (Schutz gegen das Zurücksetzen der WSL-Distribution, **kein** Ersatz für eine Kopie auf einem Stick oder im Passwortmanager). Er gehört **nicht** ins Repository (`.gitignore` schließt `*.key` aus) und sollte gesichert werden; ohne ihn lassen sich keine Regeln mehr signieren, und ein neuer Schlüssel braucht eine neue PS5-Medic-Version. Wer die Datei hat, kann Regeln signieren, die jede Konsole annimmt.
- **Schlüsselwechsel:** neuen öffentlichen Schlüssel in `trusted_keys.cpp` **zusätzlich** eintragen, Version ausliefern, erst später den alten entfernen. Eine Sperrliste oder ein Ablaufdatum gibt es nicht; bei einem verlorenen oder kompromittierten Schlüssel hilft nur eine neue Version mit neuem Schlüssel.

**Fehlercodes im selben Weg (9. Nov. 2026):** `knowledge/error_codes.json` lässt sich genauso aktualisieren (`KnowledgeKind::ErrorCodes` in `rule_update.cpp`): eigene Dateien im Update-Ordner (`error_codes.json`, `.sha256`, `.sig`), dieselben Schlüssel und Prüfungen (Signatur, Schema der Fehlercode-Datei, nur neuere `db_version`), eigene Sicherung (`backups/error_codes_<Zeit>.json`), eigene Einträge in `update.log` (`error_codes_update`, `error_codes_rollback`, `error_codes_download`) und eigene Hochwassermarke. Eine Regeldatei lässt sich nie als Fehlercode-Datei installieren und umgekehrt (die Schemas unterscheiden sich), und eine Signatur über die eine Datei gilt nicht für die andere. Im Bildschirm *Regel-Update* erscheint das Angebot der Fehlercodes unter dem der Regeln; ✕ installiert, was bereit ist, △ nimmt zurück, wo es etwas zurückzunehmen gibt. Der Download der Fehlercodes ist optional: fehlen sie auf dem Server (404) oder scheitert nur dieser Teil, gelten die Regeln trotzdem und der Bildschirm nennt den Grund. **`CLAUDE.md` nennt noch „nur zwei GET-Abrufe“ und müsste auf „bis zu vier“ geändert werden** (Marc entscheidet; nicht geändert).

**Veröffentlichen auf GitHub (Standardweg):** Regeln ändern, `db_version` hochzählen, `make test`, `make publish-rules` (signiert nach `rules-update/`, prüft die Signatur), committen und pushen; erst danach ist der Stand über `raw.githubusercontent.com` abrufbar (der Cache dort braucht bis zu etwa 5 Minuten). Ein Test (`published_rules_tests`) prüft, dass die Dateien in `rules-update/` gültig signiert und nicht neuer als `knowledge/rules.json` sind.

**Ablauf für den Autor (eigener Server):** Regeln ändern, `db_version` hochzählen, `make test`, `make sign-rules` (schreibt `dist/rules/rules.json` und `error_codes.json` samt `.sig` und prüft sie gegen den Schlüssel; bei den Fehlercodes `db_version` hochzählen, wenn sich der Inhalt ändert), `make serve-rules` (oder `dist/rules/` auf einem beliebigen HTTP-Server ablegen). Einmalig die Adresse auf die Konsole schreiben: `make set-update-url PS5_HOST=<ip> UPDATE_URL=http://<ip-des-servers>:8000/`.

**Auf der Konsole bestätigt (8. Nov. 2026, PS5 Pro, FW 12.00):** Download von `http://192.168.1.98:8000/` (Python-Server auf dem PC, an die LAN-Adresse gebunden): Der Server-Log zeigt beide Abrufe von der Konsole, der Bildschirm bot die Datei mit „Signatur gültig (Schlüssel 04de66c8…)“ an, die Installation steht in `update.log` mit `auth: signature` und der Schlüssel-ID. Eine manipulierte Datei (neuere Version, alte Signatur) wurde mit „Die Signatur passt nicht zur Datei“ abgelehnt und nicht angeboten. Der Rollback wurde schon beim Datei-Update bestätigt. **Namensauflösung** (`getaddrinfo`, mit einem Einmal-Payload geprüft): `localhost` und der Rechnername des PCs im LAN werden aufgelöst (der Name ergab 192.168.1.98); öffentliche Namen (`example.org`) lösen sich auf dieser Konsole **nicht** auf, vermutlich ist ihr DNS bewusst eingeschränkt. Die UI meldet das als „Name lässt sich nicht auflösen, nimm eine IP-Adresse“. **`https://` (18. Nov. 2026, PS5, 192.168.1.52):** Download der Regeln von GitHub über das eingebaute BearSSL bestätigt, Signatur gültig (Commit `d7fba31`); `raw.githubusercontent.com` wurde dabei aufgelöst. Der frühere Weg über `libSceHttp2` scheiterte an der Kette von GitHub (`0x8095f00c`) und ist entfernt (siehe Abschnitt 1). **Nicht auf der Konsole geprüft:** Verhalten bei blockierter Firewall über die Meldungen hinaus. Löst eine Konsole `raw.githubusercontent.com` nicht auf (wie `example.org` am 8. Nov.), bleibt nur der Weg über eine eigene Adresse.

**Prüfung** (`check_rule_update`, schreibt nichts), in dieser Reihenfolge; jeder Fehler nennt eine Kennung, die die UI übersetzt:
1. Datei da? (`no_rules_file`) · höchstens 2 MiB (`too_large`) · lesbar (`unreadable`)
2. Liegt `rules.json.sig` da: Signatur lesen und prüfen (`sig_malformed`, `sig_unknown_key`, `sig_invalid`); die Prüfsumme wird dann ignoriert, Ergebnis „Signatur gültig (Schlüssel …)“
3. Sonst (Datei-Update): `rules.json.sha256` vorhanden und mit 64 Hex-Zeichen als erstem Wort, wie `sha256sum` es schreibt (`no_checksum`), und SHA-256 stimmt (`checksum_mismatch`); Ergebnis „nur Prüfsumme“
4. Regel-Pruefer (`load_rules_from_string`) akzeptiert die Datei, auch das Schema (`invalid_rules`, mit Meldung)
5. `db_version` ist **neuer** als die laufende (`not_newer`; gleich oder älter wird abgelehnt)

**Installation** (`install_rule_update`, erst nach ✕): prüft erneut · sichert die bisherige `/data/ps5medic/knowledge/rules.json` nach `backups/rules_<Zeit>.json` und liest das Backup gegen · schreibt die neue Datei atomar und liest sie gegen (bei Abweichung wird die alte zurückgeschrieben) · legt die Update-Dateien nach `update/installed/<Zeit>/` (nie löschen) · schreibt eine Zeile in `knowledge/update.log`. Die laufende App lädt die neuen Regeln sofort; die bisherigen Befunde gelten nicht mehr, der Start zeigt „noch nicht geprüft".

**Rollback** (`rollback_rule_update`, △ dann ✕): stellt die gesicherte Datei Byte für Byte wieder her, die ersetzte bleibt als `backups/rules_rolledback_<Zeit>.json` erhalten. Gab es vorher keine Datei im Wissensordner (es galten die mitgelieferten Regeln), wird die installierte beiseite gelegt.

**Schreibgrenze:** Alles liegt unter `/data/ps5medic/knowledge/`; ein Test prüft, dass sich sonst nichts ändert. Ohne Schreibzugriff, mit falscher Prüfsumme oder ungültigem Zeitstempel bleibt der Dateibaum unverändert.

Wo die App Regeln und Fehlercodes sucht: in `/data/ps5medic/knowledge/` und in der mitgelieferten `/data/homebrew/PS5MEDIC/`; **es gilt die Datei mit der höheren `db_version`**, bei Gleichstand die in `knowledge/` (seit 13. Nov. 2026; vorher hatte `knowledge/` immer Vorrang, und eine alte Kopie von `make deploy-ui` verdeckte die neue Lieferung: 40 statt 153 Codes). `make launch-ui` überschreibt die Datei in `knowledge/` mit dem Stand des Repos, das ist der Entwicklungsweg. **Wer Regeln ändert, muss die `db_version` hochzählen**, sonst gewinnt eine ältere Kopie mit derselben Nummer.

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

- **Beiträge** auf GBAtemp/Discord: entfallen vorerst (Entscheidung vom 8. Nov. 2026; das Repository ist seit dem 16. Nov. 2026 öffentlich, Beiträge in Foren nur auf Marcs Anweisung). Der Entwurf in `docs/TESTER.md` bleibt als Vorlage.
- **GitHub-Release:** seit 16. Nov. 2026 `v0.4.0` (Repository öffentlich). Lizenz: GPL-3.0-or-later (siehe `docs/PHASE4.md`). Die Issue-Vorlage hat nur in einem Repository mit Issues einen Sinn.
- **Tester-Fixtures** entstehen erst, wenn es Berichte gibt.
