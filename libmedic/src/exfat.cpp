// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/exfat.hpp"

#include <algorithm>
#include <cctype>

namespace medic {
namespace {

constexpr std::size_t kMaxDirBytes = 8u << 20;      // Wurzelverzeichnis: hoechstens 8 MiB lesen
constexpr std::uint32_t kMaxChain = 1u << 22;       // Schutz vor Schleifen in der FAT

std::uint32_t rd32(const std::string& b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3])) << 24);
}
std::uint64_t rd64(const std::string& b, std::size_t off) { return static_cast<std::uint64_t>(rd32(b, off)) | (static_cast<std::uint64_t>(rd32(b, off + 4)) << 32); }

void put(std::string& b, std::size_t off, std::uint64_t v, int n) {
    for (int i = 0; i < n; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
}

bool same_name_ascii(const std::string& utf16le, std::size_t chars, const std::string& wanted) {
    if (chars != wanted.size()) return false;
    for (std::size_t i = 0; i < chars; ++i) {
        const unsigned lo = static_cast<unsigned char>(utf16le[2 * i]), hi = static_cast<unsigned char>(utf16le[2 * i + 1]);
        if (hi != 0 || std::tolower(static_cast<int>(lo)) != std::tolower(static_cast<unsigned char>(wanted[i]))) return false;
    }
    return true;
}

}  // namespace

bool ExfatImage::read_raw(std::uint64_t offset, std::size_t len, std::string& out) const {
    std::string tmp;
    if (!fs_->read_range(path_, offset, len, tmp) || tmp.size() != len) return false;
    out = std::move(tmp);
    return true;
}

std::uint64_t ExfatImage::cluster_offset(std::uint32_t c) const { return heap_offset_ + static_cast<std::uint64_t>(c - 2) * cluster_bytes_; }

bool ExfatImage::open(const IFileSystem& fs, const std::string& path, ExfatImage& out) {
    std::string b;
    if (!fs.read_range(path, 0, 512, b) || b.size() < 112 || b.compare(3, 8, "EXFAT   ") != 0) return false;
    const unsigned bps = static_cast<unsigned char>(b[108]), spc = static_cast<unsigned char>(b[109]);
    if (bps < 9 || bps > 12 || spc > 25 || bps + spc > 25) return false;
    ExfatImage img;
    img.fs_ = &fs;
    img.path_ = path;
    const std::uint64_t sector = std::uint64_t{1} << bps;
    img.cluster_bytes_ = std::uint64_t{1} << (bps + spc);
    img.fat_offset_ = static_cast<std::uint64_t>(rd32(b, 80)) * sector;
    img.heap_offset_ = static_cast<std::uint64_t>(rd32(b, 88)) * sector;
    img.cluster_count_ = rd32(b, 92);
    img.root_cluster_ = rd32(b, 96);
    if (img.cluster_count_ == 0 || img.root_cluster_ < 2 || img.root_cluster_ >= img.cluster_count_ + 2 || img.fat_offset_ == 0 || img.heap_offset_ == 0) return false;
    out = std::move(img);
    return true;
}

bool ExfatImage::next_cluster(std::uint32_t c, std::uint32_t& next) const {
    if (c < 2 || c >= cluster_count_ + 2) return false;
    std::string e;
    if (!read_raw(fat_offset_ + static_cast<std::uint64_t>(c) * 4, 4, e)) return false;
    next = rd32(e, 0);
    return true;
}

bool ExfatImage::find_root_file(const std::string& name, ExfatFile& out) const {
    if (!fs_) return false;
    // Wurzelverzeichnis (immer eine Kette in der FAT) ganz lesen
    std::string dir;
    std::uint32_t c = root_cluster_;
    for (std::uint32_t steps = 0; steps < kMaxChain && dir.size() < kMaxDirBytes; ++steps) {
        std::string blk;
        if (!read_raw(cluster_offset(c), static_cast<std::size_t>(cluster_bytes_), blk)) break;
        dir += blk;
        std::uint32_t next = 0;
        if (!next_cluster(c, next) || next < 2 || next >= cluster_count_ + 2) break;  // Ende der Kette (0xFFFFFFFF) oder Fehler
        c = next;
    }
    for (std::size_t i = 0; i + 32 <= dir.size(); i += 32) {
        const unsigned type = static_cast<unsigned char>(dir[i]);
        if (type == 0x00) break;  // Ende des Verzeichnisses
        if (type != 0x85) continue;
        const std::size_t secondary = static_cast<unsigned char>(dir[i + 1]);
        const bool is_dir = (static_cast<unsigned char>(dir[i + 4]) & 0x10) != 0;
        if (secondary < 2 || i + 32 * (secondary + 1) > dir.size() || static_cast<unsigned char>(dir[i + 32]) != 0xC0) continue;
        const std::size_t stream = i + 32;
        const std::size_t name_len = static_cast<unsigned char>(dir[stream + 3]);
        std::string utf16;
        for (std::size_t k = 2; k <= secondary; ++k) {
            const std::size_t e = i + 32 * k;
            if (static_cast<unsigned char>(dir[e]) != 0xC1) break;
            utf16.append(dir, e + 2, 30);
        }
        if (is_dir || utf16.size() < name_len * 2 || !same_name_ascii(utf16, name_len, name)) continue;
        out.first_cluster = rd32(dir, stream + 20);
        out.size = rd64(dir, stream + 24);
        out.no_fat_chain = (static_cast<unsigned char>(dir[stream + 1]) & 0x02) != 0;
        return out.first_cluster >= 2;
    }
    return false;
}

bool ExfatImage::read(const ExfatFile& f, std::uint64_t offset, std::size_t len, std::string& out) const {
    if (!fs_ || f.first_cluster < 2 || offset > f.size || len > f.size - offset) return false;
    std::string result;
    result.reserve(len);
    std::uint64_t cluster_index = offset / cluster_bytes_;
    std::uint64_t in_cluster = offset % cluster_bytes_;
    std::uint32_t c = f.first_cluster;
    if (f.no_fat_chain) {
        c += static_cast<std::uint32_t>(cluster_index);
    } else {
        for (std::uint64_t i = 0; i < cluster_index; ++i) {
            if (i >= kMaxChain) return false;
            std::uint32_t next = 0;
            if (!next_cluster(c, next) || next < 2 || next >= cluster_count_ + 2) return false;
            c = next;
        }
    }
    while (result.size() < len) {
        if (c < 2 || c >= cluster_count_ + 2) return false;
        const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(cluster_bytes_ - in_cluster, len - result.size()));
        std::string part;
        if (!read_raw(cluster_offset(c) + in_cluster, take, part)) return false;
        result += part;
        in_cluster = 0;
        if (result.size() >= len) break;
        if (f.no_fat_chain) {
            ++c;
        } else {
            std::uint32_t next = 0;
            if (!next_cluster(c, next)) return false;
            c = next;
        }
    }
    out = std::move(result);
    return true;
}

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
