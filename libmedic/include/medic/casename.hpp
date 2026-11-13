// SPDX-License-Identifier: GPL-3.0-or-later
// Hilfen fuer Umbenennungen, die nur die Gross-/Kleinschreibung aendern (Il2cpp... -> Il2Cpp...). Auf exFAT und FAT unterscheidet das
// Dateisystem die Schreibweise nicht: exists() findet dort den alten UND den neuen Namen, auch wenn nur einer im Ordner steht. Ob ein Name
// wirklich so im Ordner steht, sagt allein die Liste des Ordners. Nur Header (Engine, Planung und PC-Test teilen sie).
#pragma once
#include "medic/platform.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace medic {

inline std::string casename_dir(const std::string& p) {
    const auto s = p.rfind('/');
    return s == std::string::npos ? std::string() : (s == 0 ? std::string("/") : p.substr(0, s));
}

inline std::string casename_base(const std::string& p) {
    const auto s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

inline std::string casename_fold(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Gleicher Ordner, gleicher Name bis auf Gross-/Kleinschreibung, aber nicht identisch.
inline bool is_case_only_rename(const std::string& from, const std::string& to) {
    return casename_dir(from) == casename_dir(to) && from != to && casename_fold(casename_base(from)) == casename_fold(casename_base(to));
}

// Steht ein Eintrag genau mit diesem Namen (Zeichen fuer Zeichen) im Ordner?
inline bool dir_has_name(const IFileSystem& fs, const std::string& path) {
    const std::string base = casename_base(path);
    for (const auto& e : fs.list_dir(casename_dir(path)))
        if (e.name == base) return true;
    return false;
}

}  // namespace medic
