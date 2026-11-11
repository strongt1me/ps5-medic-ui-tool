// SPDX-License-Identifier: GPL-3.0-or-later
// Screenshot-Hook fuer Entwicklung und Tests (Vertrag von PS5 DevLoop, GPL-3.0-or-later, https://github.com/griches/PS5_DevLoop): Existiert die
// Anforderungsdatei, entfernt die UI sie und schreibt das AKTUELLE Bild der UI (ihr eigener Canvas, nicht der Bildschirm) als NEUE PNG-Datei in
// einen eigenen Ordner; ein Werkzeug (DevLoop-Payload, FTP) holt sie ab und loescht sie. Ohne Anforderungsdatei geschieht nichts. Die
// Datei entsteht nur auf Anforderung und nur im eigenen Ordner; sie kann Befunde, Pfade und Titelnamen zeigen und verlaesst die Konsole nur,
// wenn jemand sie abholt. Gedacht fuer Entwicklung, nicht fuer Endnutzer.
#pragma once
#include "medic_ui/canvas.hpp"

#include <string>

namespace medic::ui {

constexpr const char* kShotRequestPath = "/data/ps5medic/devloop-shot";
constexpr const char* kShotFolder = "/data/ps5medic/screenshots";
constexpr int kShotMaxFiles = 30;  // liegen so viele Bilder im Ordner, wird nichts mehr geschrieben (niemand holt sie ab)

// Prueft die Anforderung und schreibt bei Erfolg die Datei. Liefert true, wenn ein Bild geschrieben wurde; `written` (optional) bekommt den
// Pfad. Mehr als einmal je Anforderung kann es nicht greifen (die Datei wird entfernt).
bool serve_shot_request(const Canvas& canvas, const std::string& request = kShotRequestPath, const std::string& folder = kShotFolder, std::string* written = nullptr);

}  // namespace medic::ui
