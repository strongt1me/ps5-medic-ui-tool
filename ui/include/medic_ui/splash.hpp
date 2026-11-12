// SPDX-License-Identifier: GPL-3.0-or-later
// Startbild (ui/assets/splash.jpg, in die Programmdatei eingebettet) und Bild-Entschluesselung fuer JPEG/PNG aus dem Speicher.
#pragma once
#include <cstddef>

#include "medic_ui/canvas.hpp"

namespace medic::ui {

// Entschluesselt ein JPEG/PNG aus dem Speicher in die Zeichenflaeche (ist das Bild nicht so gross wie `out`, wird es skaliert).
// false bei Fehler; `out` bleibt dann unveraendert.
bool decode_image(const unsigned char* data, std::size_t size, Canvas& out);

// Das eingebettete Startbild (1920x1080) in `out` (muss 1920x1080 sein). false, wenn keins eingebettet ist oder es sich nicht lesen laesst.
bool load_splash(Canvas& out);
// Das eingebettete Hintergrundbild (ui/assets/background.jpg, 1920x1080) in `out` (muss 1920x1080 sein). false, wenn keins eingebettet ist.
bool load_background(Canvas& out);

}  // namespace medic::ui
