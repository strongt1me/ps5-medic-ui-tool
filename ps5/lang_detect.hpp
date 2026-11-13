// SPDX-License-Identifier: GPL-3.0-or-later
// Sprache der Meldungen fuer Programme ohne die UI-Texte (native App vor dem Dienst, Installer): zuerst die gewaehlte Sprache aus
// /data/ps5medic/config.ini (language=xx), sonst die Systemsprache, die der Dienst nach jedem Start in /data/ps5medic/system_language
// ablegt, sonst Englisch. Index: 0 de, 1 en, 2 fr, 3 it, 4 es.
//
// Bewusst nur fopen/fread/fclose und eigene Schleifen: die native App bindet ueber die Laufzeit libc.prx nur ein, was dort vorhanden ist.
// Ein Bau mit fgets startete auf der Konsole gar nicht (13. Nov. 2026, App brach vor main() ab). Keine neuen Importe in der App!
#pragma once
#include <cstddef>
#include <cstdio>

namespace medic::lang {

// Beginnt s mit dem Text prefix?
inline bool starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) return false;
        ++s;
        ++prefix;
    }
    return true;
}

inline int index_of(const char* code) {
    static const char* const codes[] = {"de", "en", "fr", "it", "es"};
    for (int i = 0; i < 5; ++i)
        if (code[0] == codes[i][0] && code[1] == codes[i][1] && (code[2] == 0 || code[2] <= ' ')) return i;  // danach Zeilenende, Leerzeichen oder Steuerzeichen
    return -1;
}

// Liest bis zu size-1 Bytes einer Datei in buf (nullterminiert); Laenge oder -1.
inline int read_small(const char* path, char* buf, std::size_t size) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    const std::size_t n = std::fread(buf, 1, size - 1, f);
    std::fclose(f);
    buf[n] = 0;
    return static_cast<int>(n);
}

inline int detect() {
    char buf[1024];
    if (read_small("/data/ps5medic/config.ini", buf, sizeof buf) >= 0) {
        // Zeilen durchgehen: "language=xx" am Zeilenanfang
        for (const char* line = buf; *line;) {
            if (starts_with(line, "language=")) {
                const int found = index_of(line + 9);
                if (found >= 0) return found;
            }
            while (*line && *line != 10) ++line;  // bis zum Zeilenumbruch
            if (*line == 10) ++line;
        }
    }
    char sys[16];
    if (read_small("/data/ps5medic/system_language", sys, sizeof sys) > 0 && index_of(sys) >= 0) return index_of(sys);
    return 1;
}

}  // namespace medic::lang
