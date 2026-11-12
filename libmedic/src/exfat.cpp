// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/exfat.hpp"

#include <algorithm>
#include <cctype>

namespace medic {
namespace {

void put(std::string& b, std::size_t off, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
}

}  // namespace

std::string build_exfat_stub(unsigned bps_shift, unsigned spc_shift, const std::string& name, const std::string& content) {
    if (bps_shift < 9 || bps_shift > 12 || bps_shift + spc_shift > 20 || name.empty() || name.size() > 15 || content.size() > (4u << 20)) return {};
    const std::uint64_t sector = std::uint64_t{1} << bps_shift;
    const std::uint64_t csize = std::uint64_t{1} << (bps_shift + spc_shift);
    const std::uint64_t file_clusters = std::max<std::uint64_t>(1, (content.size() + csize - 1) / csize);
    const std::uint64_t clusters = 1 + file_clusters;  // Cluster 2: Wurzelverzeichnis, danach die Datei
    const std::uint64_t fat_sectors = ((clusters + 2) * 4 + sector - 1) / sector;
    const std::uint64_t fat_offset = 24, heap_offset = fat_offset + fat_sectors;
    const std::uint64_t spc = std::uint64_t{1} << spc_shift;
    const std::uint64_t total_sectors = heap_offset + clusters * spc;
    std::string img(static_cast<std::size_t>(total_sectors * sector), '\0');

    // Kopf
    img[0] = '\xEB';
    img[1] = '\x76';
    img[2] = '\x90';
    img.replace(3, 8, "EXFAT   ");
    put(img, 72, total_sectors, 8);
    put(img, 80, fat_offset, 4);
    put(img, 84, fat_sectors, 4);
    put(img, 88, heap_offset, 4);
    put(img, 92, clusters, 4);
    put(img, 96, 2, 4);
    put(img, 104, 0x0100, 2);
    img[108] = static_cast<char>(bps_shift);
    img[109] = static_cast<char>(spc_shift);
    img[110] = 1;
    img[510] = '\x55';
    img[511] = '\xAA';

    // FAT: Eintraege 0 und 1, Wurzelverzeichnis (Cluster 2) als einzelner Cluster, Datei als Kette ab Cluster 3
    const std::size_t fat = static_cast<std::size_t>(fat_offset * sector);
    put(img, fat + 0, 0xFFFFFFF8u, 4);
    put(img, fat + 4, 0xFFFFFFFFu, 4);
    put(img, fat + 8, 0xFFFFFFFFu, 4);
    for (std::uint64_t i = 0; i < file_clusters; ++i) put(img, fat + static_cast<std::size_t>(4 * (3 + i)), i + 1 == file_clusters ? 0xFFFFFFFFu : 3 + i + 1, 4);

    // Wurzelverzeichnis: Dateieintrag, Stream-Erweiterung, ein Namenseintrag
    const std::size_t root = static_cast<std::size_t>(heap_offset * sector);
    img[root] = '\x85';
    img[root + 1] = 2;
    put(img, root + 4, 0x20, 2);  // Archiv
    const std::size_t st = root + 32;
    img[st] = '\xC0';
    img[st + 1] = 0x03;  // AllocationPossible und NoFatChain
    img[st + 3] = static_cast<char>(name.size());
    put(img, st + 8, content.size(), 8);
    put(img, st + 20, 3, 4);
    put(img, st + 24, content.size(), 8);
    const std::size_t nm = root + 64;
    img[nm] = '\xC1';
    for (std::size_t i = 0; i < name.size(); ++i) img[nm + 2 + 2 * i] = name[i];
    // Dateiinhalt ab Cluster 3
    const std::size_t data = static_cast<std::size_t>((heap_offset + spc) * sector);
    img.replace(data, content.size(), content);
    return img;
}

}  // namespace medic
