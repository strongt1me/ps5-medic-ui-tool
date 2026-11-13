// SPDX-License-Identifier: GPL-3.0-or-later
// Sprache der Meldungen fuer Programme ohne die UI-Texte (native App vor dem Dienst, Installer): zuerst die gewaehlte Sprache aus
// /data/ps5medic/config.ini (language=xx), sonst die Systemsprache, die der Dienst nach jedem Start in /data/ps5medic/system_language
// ablegt, sonst Englisch. Index: 0 de, 1 en, 2 fr, 3 it, 4 es.
#pragma once
#include <cstdio>
#include <cstring>

namespace medic::lang {

inline int index_of(const char* code) {
    static const char* const codes[] = {"de", "en", "fr", "it", "es"};
    for (int i = 0; i < 5; ++i)
        if (std::strncmp(code, codes[i], 2) == 0 && (code[2] == 0 || code[2] <= ' ')) return i;  // danach Zeilenende, Leerzeichen oder Steuerzeichen
    return -1;
}

inline int detect() {
    if (std::FILE* f = std::fopen("/data/ps5medic/config.ini", "r")) {
        char line[160];
        int found = -1;
        while (found < 0 && std::fgets(line, sizeof line, f))
            if (std::strncmp(line, "language=", 9) == 0) found = index_of(line + 9);
        std::fclose(f);
        if (found >= 0) return found;
    }
    if (std::FILE* f = std::fopen("/data/ps5medic/system_language", "r")) {
        char line[16] = {0};
        const bool ok = std::fgets(line, sizeof line, f) != nullptr;
        std::fclose(f);
        if (ok && index_of(line) >= 0) return index_of(line);
    }
    return 1;
}

}  // namespace medic::lang
