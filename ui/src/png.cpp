#include "medic_ui/png.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

namespace medic::ui {

namespace {

std::uint32_t crc32(const std::uint8_t* d, std::size_t n, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

}  // namespace

bool write_png(const std::string& path, const Canvas& c, int scale_down) {
    if (scale_down < 1) scale_down = 1;
    const int w = c.width() / scale_down, h = c.height() / scale_down;
    if (w < 1 || h < 1) return false;

    // Rohdaten: je Zeile Filterbyte 0 + RGB.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (1 + 3 * static_cast<std::size_t>(w)));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < scale_down; ++dy)
                for (int dx = 0; dx < scale_down; ++dx) {
                    const Color px = c.at(x * scale_down + dx, y * scale_down + dy);
                    r += (px >> 16) & 0xFF;
                    g += (px >> 8) & 0xFF;
                    b += px & 0xFF;
                }
            const unsigned n = static_cast<unsigned>(scale_down * scale_down);
            raw.push_back(static_cast<std::uint8_t>(r / n));
            raw.push_back(static_cast<std::uint8_t>(g / n));
            raw.push_back(static_cast<std::uint8_t>(b / n));
        }
    }

    // zlib-Strom aus unkomprimierten ("stored") Bloecken.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t s1 = 1, s2 = 0;
    for (std::uint8_t byte : raw) {
        s1 = (s1 + byte) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    std::size_t pos = 0;
    bool last = false;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (!last);
    put32(z, (s2 << 16) | s1);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(w));
    put32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 Bit, RGB
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

}  // namespace medic::ui
