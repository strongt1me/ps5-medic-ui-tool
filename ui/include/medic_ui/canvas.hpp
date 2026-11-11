// SPDX-License-Identifier: GPL-3.0-or-later
// Software-Zeichenflaeche (32-Bit ARGB) fuer die Vollbild-UI. Plattformunabhaengig: Am PC wird sie als PNG
// geschrieben (Vorschau, Tests), auf der Konsole spaeter in den VideoOut-Framebuffer kopiert.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace medic::ui {

using Color = std::uint32_t;  // 0xAARRGGBB

constexpr Color rgb(unsigned r, unsigned g, unsigned b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }

// Mischt `src` (mit eigenem Alpha) ueber `dst`; `cover` (0..255) skaliert das Alpha zusaetzlich (Kantenglaettung).
Color blend(Color dst, Color src, unsigned cover = 255);

enum class PadGlyph { Cross, Circle, Square, Triangle, UpDown, LeftRight, Shoulders };

class Canvas {
public:
    Canvas(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    const std::uint32_t* data() const { return px_.data(); }
    Color at(int x, int y) const;  // ausserhalb: 0

    void clear(Color c);
    void put(int x, int y, Color c, unsigned cover = 255);  // geclippt
    void fill_rect(int x, int y, int w, int h, Color c);
    void fill_round_rect(int x, int y, int w, int h, int radius, Color c);
    void stroke_round_rect(int x, int y, int w, int h, int radius, int thickness, Color c);
    // Verlaeufe und Zeichenhilfen fuer die Gestaltung (alles kantengeglaettet).
    void fill_vgradient(int x, int y, int w, int h, Color top, Color bottom);
    void fill_round_rect_vgradient(int x, int y, int w, int h, int radius, Color top, Color bottom);
    void fill_disc(float cx, float cy, float r, Color c);
    void glow(int cx, int cy, int radius, Color c, unsigned max_alpha);  // weicher Lichtfleck, nach aussen ausklingend
    void copy_from(const Canvas& other);  // gleiche Groesse, sonst nichts
    void stroke_polyline(const std::vector<std::pair<float, float>>& pts, float width, Color c);
    // Controller-Symbole als Vektorform (unabhaengig von der Schrift). size = Kantenlaenge des Feldes.
    void draw_pad_glyph(PadGlyph g, int cx, int cy, int size, Color c);

private:
    int w_, h_;
    std::vector<std::uint32_t> px_;
};

}  // namespace medic::ui
