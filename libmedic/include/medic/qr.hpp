// SPDX-License-Identifier: GPL-3.0-or-later
// QR-Code fuer den Bericht. Encoder: Nayukis QR-Code-Generator (MIT, third_party/qrcodegen).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace medic {

struct QrCode {
    int size = 0;                       // Module je Seite, 0 = leer
    std::vector<std::uint8_t> modules;  // size*size, 1 = dunkel

    bool dark(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size && modules[static_cast<std::size_t>(y * size + x)] != 0; }
};

// Kodiert Text mit Fehlerkorrektur M. false, wenn er nicht in einen QR-Code passt (rund 2300 Byte bei M).
bool qr_encode(const std::string& text, QrCode& out);

// Text-Darstellung (zwei Zeilen je Modulreihe als Halbbloecke, mit Ruhezone) zum Pruefen im Terminal.
std::string qr_to_text(const QrCode& qr);

}  // namespace medic
