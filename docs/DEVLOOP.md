# Medic-UI mit PS5 DevLoop prüfen (Entwicklung, 11. Nov. 2026)

[PS5 DevLoop](https://github.com/griches/PS5_DevLoop) (Gary Riches, GPL-3.0-or-later) ist ein Payload mit Python-Client: Es erzeugt ein virtuelles DualSense (Tasten drücken) und liefert Bilder vom laufenden Titel (`grab`) oder vom ganzen Bildschirm (`shot`). Damit lässt sich die Medic-UI **auf der Konsole** bedienen und ansehen, statt nur in der PC-Vorschau (`medic-ui-preview`). **Nur ein Entwicklungswerkzeug**: Medic selbst speist nie Eingaben ein, der Controller-Test (D14) braucht ein echtes Pad.

## Was im Repository ist

Der **Screenshot-Hook** der UI (`ui/include/medic_ui/shot_hook.hpp`, `ui/src/shot_hook.cpp`, aufgerufen in `ps5/ui_main.cpp` alle vier Bilder): Existiert `/data/ps5medic/devloop-shot`, entfernt die UI die Datei (der Entfernen-Aufruf ist die Prüfung, er scheitert sonst) und schreibt das **aktuelle Bild ihres eigenen Canvas** (1920 × 1080, nicht der Bildschirm) als neue PNG-Datei nach `/data/ps5medic/screenshots/medic-<ms>.png`. Ein Werkzeug holt sie ab und löscht sie. Schutz: nur auf Anforderung, nur im eigenen Ordner, höchstens 30 Dateien dort (danach schreibt die UI nichts mehr, weil niemand abholt). Das Bild kann Befunde, Pfade und Titelnamen zeigen und verlässt die Konsole nur, wenn jemand es abholt. Vertrag und Idee stammen aus der `AGENTS.md` von DevLoop; Code von DevLoop ist nicht übernommen.

## So nutzt man es (Eintrag für `titles.json` von DevLoop)

```json
"medic": {
  "title_id": "FAKE42008",
  "request": "/data/ps5medic/devloop-shot",
  "folder": "/data/ps5medic/screenshots"
}
```

Dann `tools/ps5loop.py grab --title medic` (Bild der Medic-UI) und `tools/ps5loop.py seq ...` für Tasten. Ohne DevLoop geht es auch von Hand: Datei `devloop-shot` per FTP nach `/data/ps5medic/` legen, kurz warten, das PNG aus `/data/ps5medic/screenshots/` holen und löschen.

## Vorbehalte (vor jedem Einsatz an der Konsole abwägen)

- DevLoop ist laut seiner README **nur auf Firmware 13.60 getestet**; die Test-Konsole hat 12.00. Die Aufrufe (`scePadVirtualDevice*`, Mbus) sind undokumentiert und können abweichen.
- `open` trennt das echte Controller-Pad, bis man PS drückt. `shot` hält die virtuelle Create-Taste 1,2 s, legt eine Datei in der Galerie an und löscht sie wieder (`sceContentDeleteByPath`).
- Der Payload lauscht **ohne Anmeldung auf Port 9112** im LAN. Nur in einem vertrauten Netz laufen lassen und danach beenden (`quit`).
- Der Client braucht am PC `JxrDecApp` und `magick`, wenn Bilder des ganzen Bildschirms entschlüsselt werden.
- Nach dem Zwischenfall vom 11. Nov. 2026 (Lasttest, siehe `docs/RULES.md`) wird DevLoop nur mit ausdrücklicher Freigabe an der Test-Konsole gestartet.
