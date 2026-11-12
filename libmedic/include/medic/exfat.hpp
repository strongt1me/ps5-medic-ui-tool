// SPDX-License-Identifier: GPL-3.0-or-later
// Minimaler, nur lesender exFAT-Leser fuer Image-Dateien (.exfat von ShadowMount+). Er haengt nichts ein und veraendert nichts: Er liest den
// Kopf, das Wurzelverzeichnis und von einer Datei nur die gewuenschten Bytebereiche (zwei kleine Lesezugriffe genuegen, um die SDK-Version
// einer eboot.bin zu lesen, auch bei Images von mehreren GB).
//
// Grenzen: nur Dateien im Wurzelverzeichnis, Namen ohne Beachtung der Gross-/Kleinschreibung (nur ASCII-Buchstaben), keine Pruefsummen,
// keine Bitmaps. Verschluesselte oder anders aufgebaute Images (.ffpkg, .ffpfs) kennt er nicht: open() liefert dann false.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "medic/platform.hpp"

namespace medic {

struct ExfatFile {
    std::uint32_t first_cluster = 0;
    std::uint64_t size = 0;
    bool no_fat_chain = false;  // zusammenhaengend gespeichert, die FAT wird nicht gebraucht
};

class ExfatImage {
public:
    // Prueft den Kopf (Kennung EXFAT, Sektor- und Clustergroesse im erlaubten Bereich). false, wenn es kein lesbares exFAT ist.
    static bool open(const IFileSystem& fs, const std::string& path, ExfatImage& out);

    // Datei im Wurzelverzeichnis suchen (kein Ordner).
    bool find_root_file(const std::string& name, ExfatFile& out) const;

    // Liest bis zu `len` Bytes ab `offset` aus der Datei; false bei Fehlern oder wenn der Bereich ueber das Dateiende hinausgeht.
    bool read(const ExfatFile& file, std::uint64_t offset, std::size_t len, std::string& out) const;

    std::uint64_t cluster_bytes() const { return cluster_bytes_; }

private:
    bool read_raw(std::uint64_t offset, std::size_t len, std::string& out) const;
    bool next_cluster(std::uint32_t c, std::uint32_t& next) const;
    std::uint64_t cluster_offset(std::uint32_t c) const;

    const IFileSystem* fs_ = nullptr;
    std::string path_;
    std::uint64_t fat_offset_ = 0, heap_offset_ = 0, cluster_bytes_ = 0, file_size_ = 0;
    std::uint32_t cluster_count_ = 0, root_cluster_ = 0;
};

// Baut ein winziges, gueltiges exFAT-Image mit einer einzigen Datei im Wurzelverzeichnis (zusammenhaengend gespeichert). Fuer den Snapshot-
// Export und Tests; bps_shift/spc_shift wie im Kopf des echten Images (Sektor 2^bps_shift, Cluster 2^(bps_shift+spc_shift) Bytes).
// Leer, wenn Name oder Groessen nicht passen (Cluster ueber 1 MiB, Name ueber 15 Zeichen, Inhalt ueber 4 MiB).
std::string build_exfat_stub(unsigned bps_shift, unsigned spc_shift, const std::string& name, const std::string& content);

}  // namespace medic
