// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/qr.hpp"

#include "qrcodegen/qrcodegen.hpp"

#include <stdexcept>

namespace medic {

bool qr_encode(const std::string& text, QrCode& out) {
    out = QrCode{};
    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        out.size = qr.getSize();
        out.modules.assign(static_cast<std::size_t>(out.size) * static_cast<std::size_t>(out.size), 0);
        for (int y = 0; y < out.size; ++y)
            for (int x = 0; x < out.size; ++x) out.modules[static_cast<std::size_t>(y * out.size + x)] = qr.getModule(x, y) ? 1 : 0;
        return true;
    } catch (const std::length_error&) {  // qrcodegen::data_too_long
        out = QrCode{};
        return false;
    }
}

std::string qr_to_text(const QrCode& qr) {
    // Fuer ein dunkles Terminal: helle Module (und die Ruhezone von 2 Modulen) sind Bloecke, dunkle Module Leerzeichen.
    // Zwei Modulreihen je Textzeile (Halbbloecke oben/unten).
    std::string out;
    const int q = 2;
    for (int y = -q; y < qr.size + q; y += 2) {
        for (int x = -q; x < qr.size + q; ++x) {
            const bool top = !qr.dark(x, y), bottom = !qr.dark(x, y + 1);
            out += top && bottom ? "\xE2\x96\x88" : top ? "\xE2\x96\x80" : bottom ? "\xE2\x96\x84" : " ";
        }
        out += "\n";
    }
    return out;
}

}  // namespace medic
