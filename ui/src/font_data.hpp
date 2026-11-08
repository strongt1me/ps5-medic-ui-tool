// Intern: Aufbau der erzeugten Schriftdaten (font_data.cpp, siehe tools/gen_font.py).
#pragma once
#include <cstddef>
#include <cstdint>

namespace medic::ui::fontdata {

struct Rec {
    std::uint32_t cp;      // Unicode-Codepoint, aufsteigend sortiert
    std::int16_t bx, by;   // Versatz nach rechts / Hoehe ueber der Grundlinie bis zur Oberkante
    std::uint8_t w, h, adv;
    std::uint32_t off;     // Offset in den Alpha-Daten (w*h Bytes)
};

struct Blob {
    int ascent, line_height;
    const Rec* recs;
    std::size_t nrecs;
    const char* hex;       // Alpha-Daten als Hex-Text
    std::size_t hexlen;
};

extern const Blob small_blob, body_blob, title_blob;

}  // namespace medic::ui::fontdata
