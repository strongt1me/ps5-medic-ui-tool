# Community: Berichte einsenden, neue Regeln, Regel-Updates

**Deutsch** · [English](en/COMMUNITY.md)

Stand 12. Nov. 2026. So entstehen neue Regeln aus Fehlern, die bei anderen Nutzern auftreten. **Nichts davon ändert die App ohne Prüfung**: Die Konsole sendet nur Rohmaterial, eine Regel schreibt, prüft und signiert der Maintainer (Marc).

```
Nutzer: Bericht (□)  ->  Issue im Repository  ->  Bot sortiert vor  ->  Maintainer prüft  ->  Regel + Fixture + Test + Übersetzungen
     ->  rules.json signieren (make sign-rules)  ->  Nutzer: Regeln aktualisieren (Mehr)
```

## 1. Bericht einsenden

Taste □ in der Befundliste legt den Bericht ab (`/data/ps5medic/reports/`) und zeigt einen QR-Code. Der Bericht ist anonymisiert (IP/MAC, Konto-IDs, Seriennummern). **Datei- und Ordnernamen bleiben drin, auch Spielnamen**: vor dem Einfügen lesen und streichen. Der Text kommt in ein Issue mit der Vorlage „Problem melden“. Der Abschnitt „Unbekannte Fehlerzeilen“ ist der wichtigste: Das sind Zeilen im ShadowMount+-Log, die nach Fehlern aussehen und zu keiner Regel passen.

Ein Knopf „hochladen“ in der App ist **noch nicht gebaut**. Er bräuchte einen Zwischenserver (ein Token in der App wäre für alle auslesbar), ein öffentliches Repository für Berichte, HTTPS auf der Konsole und eine ausdrückliche Entscheidung, die Netzregel zu erweitern (heute: Netz nur für den Regel-Download). Bis dahin: Bericht kopieren und ins Issue einfügen.

## 2. Vorsortieren (Bot)

`.github/workflows/triage.yml` läuft bei jedem neuen oder bearbeiteten Issue und ruft `tools/triage_report.py` auf:

- liest den Bericht **als Daten** (der Issue-Text geht nur über eine Umgebungsvariable in das Werkzeug, nie in eine Shell-Zeile; Erwähnungen und HTML werden entschärft, Längen begrenzt),
- schreibt einen Kommentar: Firmware, Modell, Versionen, Befunde, unbekannte Zeilen, gesetzte Etiketten (`bericht`, `neue-zeilen`, `sauber`, `ohne-bericht`),
- bildet für jede unbekannte Zeile einen **Fingerabdruck** (Zahlen, Zeiten, Pfade und Titel-IDs herausgerechnet), damit gleichartige Zeilen aus verschiedenen Berichten zusammenfallen,
- erzeugt montags ein Issue „Unbekannte Fehlerzeilen: Rangliste“ (Etikett `rangliste`): in wie vielen Berichten, auf welchen Firmwares.

Der Bot hat nur die Rechte `contents: read` und `issues: write`, keine Geheimnisse, und führt nichts aus dem Issue aus. Es wird keine KI aufgerufen. Die Etiketten `bericht`, `neue-zeilen`, `sauber`, `ohne-bericht` und `rangliste` legt GitHub beim ersten Setzen selbst an.

## 3. Aus einer Zeile eine Regel machen (Maintainer)

Ab zwei bis drei unabhängigen Berichten mit demselben Fingerabdruck lohnt sich eine Regel:

```bash
gh issue list --state all --limit 500 --json number,title,body,url > issues.json
python3 tools/triage_report.py cluster issues.json                   # Rangliste
python3 tools/triage_report.py draft   issues.json <fingerabdruck>   # Regelentwurf (JSON mit TODO-Feldern)
```

Der Entwurf ist nur ein Gerüst (Id `community.<hash>`, Bedingung auf `smp.log.lines`, Status `todo`). Fertig wird die Regel erst mit den üblichen Pflichten (`README.md`, „Mitmachen“): **Primärquelle** (Issue, README, Quelltext; nicht nur ein Bericht), Klartext in Deutsch und Englisch (dazu Französisch, Italienisch, Spanisch in `knowledge/l10n/`, `tools/l10n_check.py`), **synthetisches** Fixture und `expect.json` (keine echten Datei- oder Spielnamen aus einem Bericht), Test, ein Fix nur wenn er reversibel ist. Die Textregel gilt: nie ein Update der Systemsoftware empfehlen.

## 4. Veröffentlichen

`db_version` in `knowledge/rules.json` hochzählen, `make sign-rules`, die vier Dateien (`rules.json`, `error_codes.json`, je `.sig`) bereitstellen: heute auf einem http-Server (`make serve-rules` oder ein Webspace) oder als Dateien zum Kopieren auf die Konsole. Die Signatur sichert, dass nur Regeln des Maintainers angenommen werden, egal woher sie kommen. Siehe `docs/PHASE5.md`.

## Was nie passiert

- Keine Regel entsteht automatisch aus einem Bericht.
- Der Bot führt keinen Text aus Berichten aus, ruft kein Netz außer GitHub selbst und verändert weder Code noch Regeln.
- Berichte werden nur im Repository des Projekts gesammelt; die Konsole sendet von sich aus nichts.
