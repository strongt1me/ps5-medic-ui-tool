// SPDX-License-Identifier: GPL-3.0-or-later
// Bauer fuer winzige exFAT-Images (Snapshot-Export und Tests). Gelesen werden Images mit ImageReader (image_reader.hpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "medic/platform.hpp"

namespace medic {

// Baut ein winziges, gueltiges exFAT-Image mit einer einzigen Datei im Wurzelverzeichnis (zusammenhaengend gespeichert). Fuer den Snapshot-
// Export und Tests; bps_shift/spc_shift wie im Kopf des echten Images (Sektor 2^bps_shift, Cluster 2^(bps_shift+spc_shift) Bytes).
// Leer, wenn Name oder Groessen nicht passen (Cluster ueber 1 MiB, Name ueber 15 Zeichen, Inhalt ueber 4 MiB).
std::string build_exfat_stub(unsigned bps_shift, unsigned spc_shift, const std::string& name, const std::string& content);

}  // namespace medic
