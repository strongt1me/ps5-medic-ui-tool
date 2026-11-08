// Minimal-invasive Bearbeitung einer ShadowMount+-config.ini: nur die betroffene Zeile aendert sich,
// Kommentare, Reihenfolge und Zeilenenden bleiben erhalten.
#pragma once
#include <string>

namespace medic {

struct IniTarget {
    std::string section;     // "" = Schluessel ohne Sektion
    std::string key;
    std::string legacy_key;  // aelterer flacher Schluessel (z. B. "scanpath"), optional
};

// Setzt key=value. Ersetzt die letzte vorhandene Zeile (dort gewinnt der letzte Wert), sonst wird eingefuegt.
// Zielwahl: existiert legacy_key flach in der Datei, wird er benutzt; sonst section/key
// (hat die Datei keine Sektionen und gibt es einen legacy_key, bleibt es beim flachen Format).
std::string ini_set_value(const std::string& text, const IniTarget& t, const std::string& value);

// Fuer Schluessel mit mehreren Zeilen (scanpath): haengt eine Zeile an, falls der Wert noch nicht vorkommt.
std::string ini_add_value(const std::string& text, const IniTarget& t, const std::string& value);

}  // namespace medic
