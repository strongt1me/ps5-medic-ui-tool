// SPDX-License-Identifier: GPL-3.0-or-later
// Fehlercode-Nachschlagewerk: laedt knowledge/error_codes.json und prueft sie (Schema wie bei rules.json). Nur lesen und
// erklaeren: Eintraege sind Text (Titel, Erklaerung, Schritte als Anleitung, Quelle), keine Aktion. Siehe docs/RULES.md.
#pragma once
#include <string>
#include <vector>

namespace medic {

struct ErrorCode {
    std::string id;                                  // "CE-108262-9"
    std::string title_de, title_en;                  // Kurztitel fuer die Liste
    std::string text_de, text_en;                    // Erklaerung (2 bis 3 Saetze)
    std::vector<std::string> steps_de, steps_en;     // sichere Schritte, nur Anleitung; beide Sprachen gleich lang
    std::vector<std::string> notes_de, notes_en;     // optionale Hinweise (z. B. Datenverlust im Safe Mode); gleich lang
    std::string source;                              // https-Adresse der Quelle
    std::vector<std::string> more_sources;           // optional: weitere Quellen fuer einzelne Aussagen
    std::string level;                               // doc (Sony-Hilfeseite) | community (keine Sony-Seite)
};

struct ErrorCodeSet {
    std::string db_version;
    std::vector<ErrorCode> codes;  // in der Reihenfolge der Datei (die Datei ist nach id sortiert)

    const ErrorCode* find(const std::string& id) const;
};

// Wo die Datei auf der Konsole liegt (make deploy-ui legt sie dort ab).
constexpr const char* kErrorCodesPath = "/data/ps5medic/knowledge/error_codes.json";

// Wirft std::runtime_error mit verstaendlicher Meldung, wenn JSON oder Schema ungueltig sind.
ErrorCodeSet load_error_codes_from_string(const std::string& json_text);
ErrorCodeSet load_error_codes_from_file(const std::string& path);

}  // namespace medic
