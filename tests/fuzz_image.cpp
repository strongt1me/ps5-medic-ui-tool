// SPDX-License-Identifier: GPL-3.0-or-later
// Zufallstest (Fuzzing) fuer den Image-Leser: verbogene exFAT-, PFS-, PFSC- und UFS2-Images duerfen den Leser nie zum Absturz bringen, nie
// ungebremst lesen oder allokieren. Gedacht fuer Sanitizer-Laeufe:
//   cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
//   build-asan/medic-fuzz-image [Durchlaeufe] [Startwert]
// Die Samen sind synthetisch (tests/fixtures); je Durchlauf werden einige Bytes zufaellig veraendert und das Image geoeffnet und gelesen.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "medic/exfat.hpp"
#include "medic/image_reader.hpp"
#include "medic/platform.hpp"

using namespace medic;

namespace {

class MemFs final : public IFileSystem {
public:
    std::string data;
    bool exists(const std::string& p) const override { return p == "/img" || p == "/"; }
    bool is_dir(const std::string& p) const override { return p == "/"; }
    std::vector<DirEntry> list_dir(const std::string& p) const override {
        if (p != "/") return {};
        DirEntry e;
        e.name = "img";
        e.size = data.size();
        return {e};
    }
    bool read_file(const std::string&, std::string&) const override { return false; }
    bool read_range(const std::string& p, std::uint64_t off, std::size_t len, std::string& out) const override {
        if (p != "/img" || off > data.size() || len > data.size() - off) return false;
        out = data.substr(static_cast<std::size_t>(off), len);
        return true;
    }
};

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void put32(std::string& b, std::size_t off, std::uint32_t v) {
    if (off + 4 > b.size()) return;
    for (int i = 0; i < 4; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
}

std::string ufs_seed() {
    std::string b(1u << 20, '\0');
    std::mt19937 rng(7);
    for (std::size_t i = 65536; i < b.size(); ++i) b[i] = static_cast<char>(rng() & 0xff);
    const std::size_t sb = 65536;
    put32(b, sb + 0x30, 32768);
    put32(b, sb + 0x34, 4096);
    put32(b, sb + 0xB8, 64);
    put32(b, sb + 0xBC, 8192);
    put32(b, sb + 0x10, 24);
    put32(b, sb + 0x55C, 0x19540119u);
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    const long runs = argc > 1 ? std::atol(argv[1]) : 20000;
    const unsigned seed = argc > 2 ? static_cast<unsigned>(std::atol(argv[2])) : 1;
    std::vector<std::string> seeds;
    const std::filesystem::path fx = std::filesystem::path(MEDIC_SOURCE_DIR) / "tests" / "fixtures";
    for (const char* f : {"g1_neg_inline_image/fs/mnt/usb0/homebrew/PPSA00001.exfat", "g1_neg_inline_ffpfs/fs/mnt/usb0/homebrew/PPSA00001.ffpfs",
                          "g1_neg_inline_ffpfsc/fs/mnt/usb0/homebrew/PPSA00001.ffpfsc"})
        seeds.push_back(slurp(fx / f));
    std::string eb(0x1200, 'e');
    seeds.push_back(build_exfat_stub(9, 3, "eboot.bin", eb));
    seeds.push_back(build_exfat_stub(12, 4, "eboot.bin", eb));
    seeds.push_back(ufs_seed());
    for (const auto& s : seeds)
        if (s.empty()) {
            std::cerr << "leerer Samen\n";
            return 2;
        }
    std::mt19937_64 rng(seed);
    long opened = 0, read_ok = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long i = 0; i < runs; ++i) {
        MemFs fs;
        fs.data = seeds[rng() % seeds.size()];
        const int edits = 1 + static_cast<int>(rng() % 12);
        for (int k = 0; k < edits; ++k) {
            // die ersten Bytes und die Gegend der Strukturen sind interessanter als der Rest
            const std::size_t span = (rng() % 3 == 0) ? fs.data.size() : std::min<std::size_t>(fs.data.size(), 70000);
            const std::size_t at = static_cast<std::size_t>(rng() % span);
            switch (rng() % 4) {
                case 0: fs.data[at] = static_cast<char>(rng() & 0xff); break;
                case 1: put32(fs.data, at, static_cast<std::uint32_t>(rng())); break;
                case 2: put32(fs.data, at, 0xFFFFFFFFu); break;
                default: put32(fs.data, at, static_cast<std::uint32_t>(rng() % 64)); break;
            }
        }
        if (rng() % 10 == 0) fs.data.resize(static_cast<std::size_t>(rng() % (fs.data.size() + 1)));  // abgeschnittenes Image
        if (fs.data.empty()) continue;
        const auto img = ImageReader::open(fs, "/img");
        if (!img) continue;
        ++opened;
        std::string out;
        if (img->read_file("/eboot.bin", 0, 4096, out)) ++read_ok;
        img->read_file("/eboot.bin", rng() % 100000, 24, out);
        img->read_file("/EBOOT.BIN", 0, 1, out);
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%ld Durchlaeufe in %.1f s, %ld Images geoeffnet, %ld mal die eboot.bin gelesen: kein Absturz\n", runs, s, opened, read_ok);
    return 0;
}
