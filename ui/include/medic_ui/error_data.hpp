// SPDX-License-Identifier: GPL-3.0-or-later
// Fehlercode-Wissen fuer die UI: knowledge/error_codes.json, eingebettet (tools/embed_error_codes.py).
#pragma once
#include "medic/error_codes.hpp"

namespace medic::ui {

extern const char* const kErrorCodesJson;

// Einmal beim ersten Aufruf geparst. Ist die eingebettete Datei ungueltig (sollte der Test ausschliessen), ist die Liste leer.
const medic::ErrorDb& embedded_error_db();

}  // namespace medic::ui
