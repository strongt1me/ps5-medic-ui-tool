// SPDX-License-Identifier: GPL-3.0-or-later
// Support-Paket: eine ZIP-Datei (ohne Kompression) mit dem Bericht in beiden Sprachen, den gelesenen Fakten und einer README, die der Nutzer
// in einem Forum oder Issue weitergeben kann. Zusaetzlich zur Anonymisierung des Berichts (IP, MAC, Konto-IDs, Seriennummern) werden
// Titel-IDs (PPSA/CUSA...), Spielnamen und die Namen der Spielordner und -images durch Platzhalter ersetzt ([TITLE-01], [GAME-01]).
// Der Schluessel (Platzhalter -> Original) liegt NUR als eigene Datei daneben und gehoert nie ins Paket.
// Nichts davon braucht Netz; geschrieben wird nur nach /data/ps5medic/support/ (neue Dateien, nie ueberschreiben, nie loeschen).
#pragma once
#include "medic/facts.hpp"
#include "medic/report.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace medic {

constexpr const char* kSupportDir = "/data/ps5medic/support";

// "support_<zeit>.zip" bzw. "support_<zeit>_key.txt" im Support-Ordner; leer bei ungueltigem Zeitstempel.
std::string support_path_for(const std::string& timestamp);
std::string support_key_path_for(const std::string& timestamp);

// Platzhalter fuer Titel-IDs und Namen. Gleiche Eingabe ergibt immer denselben Platzhalter, Reihenfolge der ersten Nennung.
class Pseudonyms {
public:
    // Sammelt aus den Fakten (smp.titles, smp.images, games.* mit "titleid"/"path"): Titel-IDs und die Namen von Spielordnern, Images und Titeln.
    void collect(const Facts& facts);
    void add_title_id(const std::string& id);
    void add_name(const std::string& name);  // zu kurze (unter 4 Zeichen) und allgemeine Namen ("games", "data" ...) werden ignoriert
    // Ersetzt alle bekannten Eintraege (laengste zuerst) und danach jede noch uebrige PPSA-/CUSA-Kennung.
    std::string apply(const std::string& text);
    // "[TITLE-01] = <Original>" je Zeile (nur fuer den Nutzer, nie im Paket).
    std::string key_text() const;
    std::size_t title_count() const { return titles_.size(); }
    std::size_t name_count() const { return names_.size(); }

private:
    std::vector<std::string> titles_, names_;
    std::string title_placeholder(std::size_t i) const;
    std::string name_placeholder(std::size_t i) const;
};

// CRC-32 (IEEE) fuer ZIP.
std::uint32_t crc32_ieee(const std::string& data);

struct ZipEntry {
    std::string name;  // nur ASCII, ohne fuehrenden Slash
    std::string data;
};
// ZIP ohne Kompression ("stored"), gueltig fuer jedes Entpackprogramm; Zeitstempel fest auf 1980-01-01 (kein Hinweis auf die Uhr der Konsole).
std::string make_zip(const std::vector<ZipEntry>& entries);

struct SupportBundle {
    std::string zip, key;
    std::size_t titles = 0, names = 0;
};
// Baut das Paket aus den Berichtsdaten. `facts_json`: bereits ausgewaehlte Fakten (z. B. wie last_facts.json), `compare_text`: optionaler
// Vergleich mit dem vorigen Check-up (leer = weglassen).
SupportBundle build_support_bundle(const ReportInput& in, const std::string& facts_json, const std::string& compare_text);

}  // namespace medic
