// SPDX-License-Identifier: GPL-3.0-or-later
// Symbolsatz der Oberflaeche: Vektorsymbole (Linien, Kreise, Fuellungen), einmal je Groesse in eine Deckkraftkarte gerechnet und danach nur noch
// eingefaerbt. Keine Bilddateien, nichts wird nachgeladen. Jedes Symbol hat eine Hauptfarbe und optional eine Akzentfarbe.
#pragma once
#include "medic_ui/canvas.hpp"

namespace medic::ui {

enum class Icon {
    Medic,        // Logo: Kreuz im abgerundeten Feld
    Pulse,        // Herzschlaglinie (Check-up)
    Stethoscope,
    History,      // Uhr
    Gear,
    Book,         // Fehlercodes
    Checklist,    // Selbsttest
    Critical,
    Warning,
    Info,
    Ok,
    Skip,
    Circle,       // nicht gelaufen
    Drive,
    Thermometer,
    Database,
    File,
    Gauge,
    Wifi,
    Flame,
    Chip,
    Pencil,
    Gamepad,
    Display,
    Wrench,
    Globe,
    Download,
    Report,
    Lightbulb,
    Undo,
    Shield,
};

// Zeichnet das Symbol mit Mittelpunkt (cx, cy) in einem Feld der Kantenlaenge `size`. accent == 0: Akzentteile in der Hauptfarbe.
void draw_icon(Canvas& c, Icon icon, int cx, int cy, int size, Color color, Color accent = 0);

// Symbol auf einem abgerundeten Farbfeld mit Verlauf (hell nach dunkel) und weissem Symbol; `size` ist die Kantenlaenge des Feldes.
void draw_icon_badge(Canvas& c, Icon icon, int x, int y, int size, Color tint);

// Hilfsfarben: heller bzw. dunkler Ton einer Farbe (Anteil 0 bis 255).
Color lighten(Color c, unsigned amount);
Color darken(Color c, unsigned amount);

}  // namespace medic::ui
