// SPDX-License-Identifier: GPL-3.0-or-later
// Entwicklungswerkzeug (kein Teil des Produkts): oeffnet jedes Spiel-Image unter /mnt/usb0/homebrew und /data/homebrew mit dem Image-Leser
// und schreibt Format, Lesbarkeit der /eboot.bin und ihre ersten Bytes nach /data/ps5medic/image_probe.log.
// Senden: nc <ip> 9021 < build-ps5/ps5medic-image-probe.elf
#include <cstdio>
#include <string>

#include "medic/image_reader.hpp"
#include "ps5_platform.hpp"

int main() {
    std::FILE* log = std::fopen("/data/ps5medic/image_probe.log", "w");
    if (!log) return 1;
    auto platform = medic::make_ps5_platform(false);
    const medic::IFileSystem& fs = platform->fs();
    for (const char* dir : {"/mnt/usb0/homebrew", "/mnt/usb0/Downloads", "/data/homebrew"}) {
        for (const auto& e : fs.list_dir(dir)) {
            const auto dot = e.name.rfind('.');
            const std::string ext = dot == std::string::npos ? "" : e.name.substr(dot);
            if (e.is_dir || (ext != ".exfat" && ext != ".ffpkg" && ext != ".ffpfs" && ext != ".ffpfsc")) continue;
            const std::string path = std::string(dir) + "/" + e.name;
            const auto img = medic::ImageReader::open(fs, path);
            if (!img) {
                std::fprintf(log, "%s: NICHT GELESEN (kein bekanntes Format oder beschaedigt)\n", path.c_str());
                std::fflush(log);
                continue;
            }
            std::string head;
            const bool ok = img->read_file("/eboot.bin", 0, 16, head);
            char hex[64] = "";
            if (ok)
                for (std::size_t i = 0; i < head.size() && i < 8; ++i) std::snprintf(hex + 2 * i, 4, "%02x", static_cast<unsigned char>(head[i]));
            std::fprintf(log, "%s: Format %s, /eboot.bin %s %s\n", path.c_str(), img->kind().c_str(), ok ? "lesbar, Anfang" : "NICHT lesbar", hex);
            std::fflush(log);
        }
    }
    std::fclose(log);
    return 0;
}
