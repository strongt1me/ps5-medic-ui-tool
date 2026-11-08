// SPDX-License-Identifier: GPL-3.0-or-later
// Schrift (vorgerenderte DejaVu-Glyphen) und Textumbruch. Eingabetext ist UTF-8; unbekannte Zeichen erscheinen als "?".
#pragma once
#include "medic_ui/canvas.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace medic::ui {

namespace fontdata {
struct Blob;
}

class Font {
public:
    enum class Id { Small, Body, Title };
    static const Font& get(Id id);

    int ascent() const { return ascent_; }
    int line_height() const { return line_height_; }
    int width(std::string_view utf8) const;
    // Zeichnet ab (x, y_top) = linke obere Ecke der Zeile. max_width > 0 schneidet rechts ab (ohne "...").
    void draw(Canvas& c, int x, int y_top, std::string_view utf8, Color color, int max_width = 0) const;

private:
    struct Glyph {
        std::uint32_t cp;
        std::int16_t bx, by;  // Versatz nach rechts / Hoehe der Oberkante ueber der Grundlinie
        std::uint8_t w, h, adv;
        std::uint32_t off;  // Offset in alpha_
    };
    explicit Font(const fontdata::Blob& blob);
    const Glyph& find(std::uint32_t cp) const;  // unbekannt -> '?'

    int ascent_ = 0, line_height_ = 0;
    std::vector<Glyph> glyphs_;  // nach Codepoint sortiert
    std::vector<std::uint8_t> alpha_;
};

// UTF-8 -> Codepoint (ungueltige Folgen ergeben U+FFFD). Rueckt `i` vor.
std::uint32_t next_codepoint(std::string_view s, std::size_t& i);

// Bricht `text` an Leerzeichen und Zeilenenden um; zu lange Woerter (Pfade) werden zeichenweise getrennt.
// max_lines > 0 begrenzt die Zeilenzahl; die letzte Zeile endet dann mit "…".
std::vector<std::string> wrap_text(const Font& f, std::string_view text, int max_width, int max_lines = 0);

// Kuerzt auf eine Zeile mit "…" am Ende.
std::string elide(const Font& f, std::string_view text, int max_width);

}  // namespace medic::ui
