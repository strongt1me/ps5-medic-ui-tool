// SPDX-License-Identifier: GPL-3.0-or-later
// Fehlercode-Nachschlagewerk: laedt knowledge/error_codes.json (PS5-Codes aus der offiziellen Liste von Sony) und
// beantwortet "Was bedeutet CE-108255-1 und was kann ich tun?". Reines Wissen, liest und schreibt nichts auf der Konsole.
//
// Aufbau der Datei: `kinds` beschreiben je Art von Fehler die Schritte zur Loesung (eigene Zusammenfassung der Sony-Seiten) und
// einen Homebrew-Hinweis fuer gejailbreakte Konsolen; `codes` ordnen jedem Code Bedeutung und Art zu; `prefixes` erklaeren die
// Code-Familien (CE, NP, ...) fuer unbekannte Codes. Der Link zur offiziellen Sony-Seite wird aus `url_template` gebildet.
#pragma once
#include <string>
#include <vector>

namespace medic {

struct ErrorKind {
    std::string id;
    std::string title_de, title_en;
    std::vector<std::string> steps_de, steps_en;  // leer bei "info" (kein Fehler)
    std::string homebrew_de, homebrew_en;         // Hinweis fuer gejailbreakte Konsolen, optional
    bool check = false;                           // ein Check-up kann hier helfen
    std::vector<std::string> related_rules;       // Regel-IDs aus rules.json, die so einen Fehler erklaeren koennen
};

struct ErrorCode {
    std::string code;  // kanonisch, Grossbuchstaben, z. B. "CE-108255-1"
    std::string kind;  // Schluessel in ErrorDb::kinds
    std::string text_de, text_en;
};

struct ErrorPrefix {
    std::string id;  // "CE"
    std::string text_de, text_en;
};

struct ErrorDb {
    std::string db_version, source, url_template;
    std::vector<ErrorPrefix> prefixes;
    std::vector<ErrorKind> kinds;
    std::vector<ErrorCode> codes;  // in der Reihenfolge der Datei (nach Familie gruppiert)

    const ErrorKind* kind(const std::string& id) const;
    const ErrorKind* kind_of(const ErrorCode& c) const { return kind(c.kind); }
    const ErrorPrefix* prefix(const std::string& id) const;
};

// Wirft std::runtime_error mit verstaendlicher Meldung, wenn JSON oder Schema ungueltig sind (unbekannte Art, doppelter Code,
// leere Texte, Code ohne gueltige Familie).
ErrorDb load_error_db_from_string(const std::string& json_text);
ErrorDb load_error_db_from_file(const std::string& path);

// Bringt Eingaben auf die kanonische Form: "ce-108255-1", "CE 108255 1" und "CE1082551" werden zu "CE-108255-1";
// "e2-8223b400" zu "E2-8223B400". Leer, wenn die Eingabe keine vollstaendige Code-Form hat.
std::string normalize_code(const std::string& input);

// Genauer Treffer nach normalize_code, sonst nullptr.
const ErrorCode* find_code(const ErrorDb& db, const std::string& input);

// Teilsuche fuer unvollstaendige Eingaben: alle Codes, die die Ziffern bzw. Buchstaben der Eingabe enthalten (ohne Bindestriche
// und Gross-/Kleinschreibung verglichen). Eingaben unter 3 Zeichen liefern nichts.
std::vector<const ErrorCode*> search_codes(const ErrorDb& db, const std::string& input);

// Familie eines Codes bzw. einer Eingabe ("CE-999999-9" -> "CE"); leer, wenn sie nicht mit einem Buchstaben und einem
// Buchstaben oder einer Ziffer beginnt.
std::string code_prefix(const std::string& input);

// Link zur offiziellen Sony-Seite des Codes (url_template mit {code_lower}); leer ohne Vorlage.
std::string support_url(const ErrorDb& db, const std::string& code);

// Alle Codes einer Familie in Dateireihenfolge.
std::vector<const ErrorCode*> codes_with_prefix(const ErrorDb& db, const std::string& prefix);

}  // namespace medic
