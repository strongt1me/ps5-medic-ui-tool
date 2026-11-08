// Minimaler PNG-Schreiber (unkomprimiert) fuer Vorschau und Tests. scale_down = ganzzahliger Verkleinerungsfaktor.
#pragma once
#include "medic_ui/canvas.hpp"

#include <string>

namespace medic::ui {

bool write_png(const std::string& path, const Canvas& c, int scale_down = 1);

}  // namespace medic::ui
