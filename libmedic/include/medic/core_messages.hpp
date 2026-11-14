// SPDX-License-Identifier: GPL-3.0-or-later
// Meldungen der Fix-Engine (fixes.cpp, engine.cpp) sind deutsch; die Oberflaeche zeigt sie ausserhalb des Deutschen auf Englisch.
#pragma once
#include <string>

namespace medic {

// Ersetzt die bekannten deutschen Fragmente durch Englisch; unbekannter Text bleibt unveraendert.
std::string core_message_en(const std::string& de);

}  // namespace medic
