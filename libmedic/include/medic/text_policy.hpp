// SPDX-License-Identifier: GPL-3.0-or-later
// Textregel (Entscheidung von Marc, 10. Nov. 2026): PS5 Medic zeigt nie ein Update der Systemsoftware/Firmware an und raet nie
// dazu, egal bei welchem Fehler. Die Regel gilt fuer jeden Text, den die App anzeigt oder in den Bericht schreibt, auch fuer
// signierte Updates der Wissensdateien: Die Lader von rules.json und error_codes.json lehnen eine Datei mit so einem Text ab.
// Erlaubt bleiben Updates anderer Dinge (ein Spiel, ShadowMount+, ps5-theme) und der Menuepfad zum Zuruecksetzen
// ("Settings > System > System Software > Reset Options").
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace medic {

// Liefert die erste Stelle, die ein Update der Systemsoftware/Firmware nennt ("Systemupdate", "Firmware aktualisieren",
// "update the system software", "neueste Systemsoftware" ...), sonst einen leeren String.
std::string find_system_update_advice(const std::string& text);

// Geht alle Strings eines JSON-Baums durch (Schluessel mit '_' am Anfang und https-Adressen ausgenommen). Bei einem Treffer
// wirft die Funktion std::runtime_error: "<was>: <Pfad>: <Treffer>".
void require_no_system_update_advice(const nlohmann::json& root, const std::string& what);

}  // namespace medic
