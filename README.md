# PS5 Medic

Vollbild-App für gejailbreakte PS5-Konsolen: prüft das Homebrew-Setup auf der Konsole selbst, erklärt Fehler in Klartext und repariert sichere Fälle mit Backup und Undo.

**Status:** Phase 0 (Host-Kern, Regeln A1/A3/B1–B15) und Phase 1 (Fix-Engine mit Journal und Undo) laufen am PC/WSL (`make test`). Die PS5-Oberfläche folgt – siehe `CLAUDE.md` und `docs/ROADMAP.md`.

| Datei | Inhalt |
|---|---|
| `CLAUDE.md` | Einstieg für Claude Code: Ziel, Stack, Grenzen, Arbeitsweise |
| `docs/SPEC.md` | Funktionen, Bedienung, Nicht-Ziele |
| `docs/ARCHITECTURE.md` | Module, Datenfluss, Regel-Format, Sicherheit |
| `docs/RULES.md` | Startkatalog mit 22 Regeln und Quellen |
| `docs/ROADMAP.md` | Phasen 0–5 und erster Prompt |
| `knowledge/` | JSON-Schema + Beispielregeln |
