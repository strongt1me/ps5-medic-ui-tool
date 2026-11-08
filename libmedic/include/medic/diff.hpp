// SPDX-License-Identifier: GPL-3.0-or-later
// Einfacher Zeilen-Diff (LCS) fuer config.ini und Autoload-Listen (Vorschau in CLI und UI).
#pragma once
#include <string>
#include <vector>

namespace medic {

struct DiffLine {
    char tag = ' ';  // ' ' unveraendert, '-' entfernt, '+' neu
    std::string text;
};

// Zerlegt nach Zeilenenden (LF/CRLF). false, wenn eine Seite mehr als `max_lines` Zeilen hat.
bool diff_lines(const std::string& old_text, const std::string& new_text, std::vector<DiffLine>& out, std::size_t max_lines = 2000);

}  // namespace medic
