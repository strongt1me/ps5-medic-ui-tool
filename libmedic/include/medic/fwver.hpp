// SPDX-License-Identifier: GPL-3.0-or-later
// Firmware-Version aus dem Rohwert von kernel_get_fw_version() (ps5-payload-sdk).
// Annahme (noch auf der Konsole zu bestaetigen): Hex-Ziffern als Dezimalzahl, 0xMMmm0000 -> "M.mm",
// z. B. 0x04030000 -> "4.03", 0x13600000 -> "13.60". Unplausible Werte ergeben "0x<hex>", damit der Rohwert sichtbar bleibt.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace medic {

inline std::string format_ps5_fw(std::uint32_t raw) {
    auto is_bcd = [](unsigned byte) { return (byte >> 4) <= 9 && (byte & 0xf) <= 9; };
    const unsigned major = (raw >> 24) & 0xff, minor = (raw >> 16) & 0xff;
    char buf[32];
    if (major == 0 || !is_bcd(major) || !is_bcd(minor)) {
        std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(raw));
    } else {
        std::snprintf(buf, sizeof buf, "%x.%02x", major, minor);
    }
    return buf;
}

}  // namespace medic
