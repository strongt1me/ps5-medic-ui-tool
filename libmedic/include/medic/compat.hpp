// SPDX-License-Identifier: GPL-3.0-or-later
// Kompatibilitaetsliste fuer ZSTD-/LZ4-Sicherungen (Projekt ps5-zstd-kompatibilitaetsliste): Ergebnis der Tester je Titel-ID. Eingebettete Tabelle
// (libmedic/src/compat_data.cpp, erzeugt von tools/import_compat.py), nur Wissen, kein Netz.
#pragma once

#include <string>

namespace medic {

struct CompatEntry {
    const char* titleid;
    const char* status;  // "ok" (alle Berichte erfolgreich), "fail" (alle fehlgeschlagen), "mixed" (widerspruechlich)
    const char* format;  // "ZSTD", "LZ4", "ZSTD/LZ4" oder "" (unbekannt)
};

extern const CompatEntry kCompatTable[];  // nach titleid sortiert
extern const unsigned kCompatCount;
extern const char* const kCompatDate;     // Stand der Liste (JJJJ-MM-TT)

// Eintrag zur Titel-ID oder nullptr (Titel steht nicht in der Liste).
const CompatEntry* find_compat(const std::string& titleid);

}  // namespace medic
