// SPDX-License-Identifier: GPL-3.0-or-later
// Liest nur das Ende einer (evtl. sehr grossen) Textdatei, statt sie ganz in den Speicher zu laden.
#pragma once
#include <cstddef>
#include <string>

#include "medic/platform.hpp"

namespace medic {

// Liest hoechstens `max_bytes` vom Ende der Datei nach `out`. Das erste, meist angeschnittene Teilstueck bis zum ersten Zeilenumbruch
// entfaellt (nur wenn tatsaechlich gekuerzt wurde und der Ausschnitt nicht genau an einem Zeilenanfang beginnt). Die Groesse kommt aus dem Verzeichniseintrag; fehlt er (z. B. bei einer Schnittstelle ohne
// Groessenangabe, oder mit Groesse 0), faellt die Funktion auf read_file zurueck und kuerzt danach. Eine leere, vorhandene Datei liefert true und einen
// leeren Text.
// false: Datei fehlt oder ist nicht lesbar.
inline bool read_tail(const IFileSystem& fs, const std::string& path, std::size_t max_bytes, std::string& out) {
    const auto slash = path.rfind('/');
    std::uint64_t size = 0;
    bool known = false;
    if (slash != std::string::npos && slash + 1 < path.size()) {
        const std::string dir = slash == 0 ? "/" : path.substr(0, slash), name = path.substr(slash + 1);
        for (const auto& e : fs.list_dir(dir))
            if (e.name == name && !e.is_dir) {
                size = e.size;
                known = true;
                break;
            }
    }
    // Groesse 0 im Verzeichniseintrag heisst "leer" oder "Groesse nicht lesbar" (PosixFs setzt dann 0): read_file klaert es. Eine leere Datei liefert true und
    // einen leeren Text; eine Datei, die nicht gelesen werden kann, liefert false (sonst gaelte ein unlesbares Log als "leer, keine Fehler").
    if (known && size == 0) known = false;
    // Beginnt der Ausschnitt genau hinter einem Zeilenumbruch, ist die erste Zeile vollstaendig und bleibt; sonst entfaellt das angeschnittene Stueck.
    auto drop_partial = [](std::string& s, bool at_line_start) {
        if (at_line_start) return;
        const auto nl = s.find('\n');
        if (nl != std::string::npos) s.erase(0, nl + 1);
    };
    if (!known || size <= max_bytes) {
        if (!fs.read_file(path, out)) return false;
        if (out.size() > max_bytes) {
            const bool at_line_start = out[out.size() - max_bytes - 1] == '\n';
            out.erase(0, out.size() - max_bytes);
            drop_partial(out, at_line_start);
        }
        return true;
    }
    // Ein Byte mehr lesen: das davor sagt, ob der Ausschnitt an einem Zeilenanfang beginnt
    if (!fs.read_range(path, size - max_bytes - 1, max_bytes + 1, out) || out.empty()) return false;
    const bool at_line_start = out[0] == '\n';
    out.erase(0, 1);
    drop_partial(out, at_line_start);
    return true;
}

// Kuerzt eine Zeile auf `max_chars` Zeichen (Bytes), damit Regex-Pruefungen nicht an einer einzigen riesigen Zeile haengen.
inline void cap_line(std::string& line, std::size_t max_chars = 2000) {
    if (line.size() > max_chars) line.resize(max_chars);
}

}  // namespace medic
