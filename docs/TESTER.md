# Mitmachen als Tester

**Deutsch** · [English](en/TESTER.md)

*English summary at the end.*

PS5 Medic ist bisher auf **einer** Konsole getestet (PS5 Pro, FW 12.00). Die Regeln stammen aus Issues, READMEs und Release-Notes und sind nur dort wirklich überprüft, wo es im Katalog `docs/RULES.md` steht. Was fehlt, sind andere Firmwares und Setups.

## Was gesucht wird

| Was | Warum |
|---|---|
| **Firmware 4.00 bis 13.60+** (die App soll dort laufen), auch die dazwischen | Regeln sind versionsgebunden (z. B. A1 ab 13.60, B3 nur 13.60, B4 nur 9.00); ob sie auf anderen Firmwares passen oder falsch feuern, weiß bisher niemand. |
| **Andere Jailbreak-Ketten**: etaHEN, kstuff-lite, Payload Manager, BD-JB, WebKit-Autoloader | Die Prozessnamen und Startzeiten unterscheiden sich (Regeln A1–A4). |
| **Verschiedene ShadowMount+-Versionen** | Config-Format und Log-Zeilen ändern sich von Version zu Version. |
| **Konsolen mit echten Problemen** | Jeder Fall, den PS5 Medic nicht erkennt, ist eine mögliche neue Regel. Besonders wertvoll: die „Unbekannten Fehlerzeilen" im Bericht. |

Ein Tester braucht nichts zu reparieren. Ein Check-up und der Bericht genügen.

## So geht's

1. Release herunterladen und die beiden Ordner `PS5MEDIC` und `FAKE42007` per FTP nach `/data/homebrew/` kopieren (siehe README).
   Oder ohne FTP: `ps5medic-launcher.elf` (eigene Datei im Release, nicht im Zip) an den ELF-Lader (Port 9021) senden, er schreibt beide Ordner selbst.
2. ShadowMount+ legt innerhalb von Sekunden die Kachel **PS5 Medic** unter „Medien“ an. Ein Druck darauf startet die App; sie sendet beim Start ihren Dienst (`ps5medicservice.elf`) selbst an den ELF-Lader, der dafür laufen muss (Port 9021). Weder websrv noch ein Launcher ist nötig.
3. **✕** auf „Check-up starten".
4. **□** in der Befundliste: legt den Bericht ab und zeigt den QR-Code. Die Datei liegt unter `/data/ps5medic/reports/`.
5. Bericht in ein Issue einfügen (Vorlage: *Problem melden*). Er ist anonymisiert. **Lies ihn vorher durch**: Datei- und Ordnernamen (auch Spielnamen) stehen drin.

Auch wenn **keine Befunde** kommen, hilft der Bericht: Er zeigt, dass eine Firmware und ein Setup sauber durchgelaufen sind.

### Wenn die Kachel nicht startet

Prüfe, ob der ELF-Lader (Port 9021) läuft und beide Ordner vollständig in `/data/homebrew/` liegen. Notfalls lässt sich nur der Diagnose-Payload `ps5medic.elf` starten (per Payload Manager oder an Port 9021). Er ist **nur lesend** und schreibt `/data/ps5medic/last_check.txt` sowie einen Snapshot nach `/data/ps5medic/snapshots/<Zeit>/`. Beides lässt sich per FTP holen.

## Datenschutz

- PS5 Medic **sendet nichts**. Netzfunktionen: der optionale Regel-Download (nur mit Adresse in `config.ini`, nur auf Bestätigung, nur zwei GET-Abrufe) und im Selbsttest ein TCP-Verbindungsversuch zum Router im eigenen Netz (keine Daten, nie das Internet); Berichte und Snapshots verlassen die Konsole nur, wenn du sie selbst weitergibst.
- Der **Bericht** ersetzt IP- und MAC-Adressen, Benutzer- und Konto-IDs und Seriennummern. **Dateinamen und Pfade bleiben**, weil sie meist die Evidenz sind.
- Der **Snapshot** enthält die Namen von Dateien und Ordnern unter den ShadowMount+-Scanpfaden, also auch Spielnamen, und das `debug.log` von ShadowMount+. Weitergeben nur, wenn du damit einverstanden bist.
- Teile nichts, was Schlüssel, Firmware oder Spiele-Inhalte enthält. PS5 Medic exportiert so etwas nicht.

## Was die Maintainer mit einem Bericht oder Snapshot tun

1. Mit `medic-cli check <snapshot>` bzw. `medic-cli report <snapshot>` am PC nachvollziehen, was PS5 Medic sieht.
2. Daraus ein **synthetisches** Fixture unter `tests/fixtures/` bauen: Struktur und die für die Regel nötigen Merkmale bleiben (Endungen, Klammern im Namen, Title-IDs, Konfigurationszeilen), echte Datei- und Spielnamen werden ersetzt. **Echte Namen kommen nie ins Repo.**
3. Neue oder korrigierte Regel in `knowledge/rules.json` mit Quelle, dazu Positiv- und Negativ-Fixture (die Tests erzwingen beides) und `docs/RULES.md` ergänzen.
4. Die Regel erscheint im nächsten Regel-Update.

## Regel-Update ausprobieren

PS5 Medic lädt **nie von selbst** etwas aus dem Netz. Ein Regel-Update ist entweder eine Datei, die du selbst auf die Konsole legst, oder ein Download, den du ausdrücklich auslöst (unten). Zuerst die Datei:

1. Neue `rules.json` aus dem Release holen und daneben die Prüfsumme anlegen: `sha256sum rules.json > rules.json.sha256`.
2. Beide Dateien per FTP oder USB-Kopie nach `/data/ps5medic/knowledge/update/` legen.
3. In PS5 Medic: Mehr, **Regeln aktualisieren**. Der Bildschirm zeigt die gefundene Version und ob die SHA-256-Prüfsumme stimmt. **✕** installiert, die bisherige Datei wird vorher gesichert (`/data/ps5medic/knowledge/backups/`), nichts wird gelöscht. Mit **△** lässt sich die letzte Installation zurücknehmen.

**Download:** im Bildschirm **Regel-Update** **□** drücken (ohne Eintrag in `config.ini` holt PS5 Medic von GitHub, `https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/`; eine andere Quelle trägst du als Zeile `update_url=http://<Adresse>/` oder `https://…` in `/data/ps5medic/config.ini` ein), die Rückfrage mit **✕** bestätigen. PS5 Medic holt `rules.json` und `rules.json.sig`, prüft die Ed25519-Signatur gegen den eingebauten Schlüssel und bietet die Datei erst dann an; installiert wird wieder erst mit **✕**. Ohne gültige Signatur wird die Datei abgelehnt, auch wenn eine Prüfsumme passt. `https://` läuft über das eingebaute TLS (BearSSL; Datum und Uhrzeit der Konsole müssen stimmen), `http://` ohne TLS; die Signatur sichert in beiden Fällen die Echtheit. Keine Weiterleitungen.

Die Prüfsumme schützt vor beschädigten oder abgeschnittenen Kopien. Sie schützt **nicht** vor einer absichtlich untergeschobenen Datei samt passender Prüfsumme; wer Dateien auf deine Konsole legen kann, kann ohnehin mehr. Nimm Regeldateien nur aus dem Release des Projekts.

## Entwurf für einen Aufruf in Foren oder auf Discord (nicht veröffentlicht, bleibt Vorlage; Beiträge in Foren nur auf Marcs Anweisung)

> **PS5 Medic sucht Tester (FW 4.00 bis 13.60+)**
>
> PS5 Medic ist eine Vollbild-App für gejailbreakte PS5, die das Homebrew-Setup prüft (ShadowMount+, kstuff, Payloads, Dateinamen, Konfiguration), Fehler in Klartext erklärt und sichere Fälle mit Backup und Rückgängig repariert. Bisher getestet nur auf einer PS5 Pro mit FW 12.00.
>
> Gesucht: andere Firmwares und Setups. Du musst nichts reparieren: Check-up starten, □ drücken, den anonymisierten Bericht in ein Issue einfügen. Auch „keine Befunde" ist ein nützliches Ergebnis. PS5 Medic sendet nichts und lädt nur auf deinen Befehl Regeln (mit Signatur) von einer Adresse, die du selbst einträgst.
>
> Link zum Release und zur Issue-Vorlage: https://github.com/strongt1me/ps5-medic-ui-tool/releases/latest und https://github.com/strongt1me/ps5-medic-ui-tool/issues/new/choose

## English summary

PS5 Medic has only been tested on one console (PS5 Pro, FW 12.00). Testers on other firmwares (4.00 to 13.60+), other jailbreak chains and ShadowMount+ versions are needed. Install the release (copy `PS5MEDIC` and `FAKE42007` to `/data/homebrew/`, with the ELF loader on port 9021 running), open the **PS5 Medic** tile, run the check-up (✕), press □ and paste the anonymized report into an issue (template: *Report a problem*). Read it first: file and folder names are still in it. Nothing is ever sent by PS5 Medic. Rule updates are files you copy to `/data/ps5medic/knowledge/update/` together with a `rules.json.sha256`, or a download you trigger yourself (□ in the rule update screen; by default from GitHub, or from your own `update_url=http://...` / `https://...` in `/data/ps5medic/config.ini`); downloads are only accepted with a valid Ed25519 signature. The rule update screen (More, *Update rules*) verifies and installs them (backup first, roll-back with △).
