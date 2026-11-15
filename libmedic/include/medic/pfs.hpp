// SPDX-License-Identifier: GPL-3.0-or-later
// Kopf eines PFS-Behaelters (.ffpfs, .ffpfsc): Magic 0x1332A0B bei Offset 8, Blockgroesse u32 bei 32, Blockzahl u64 bei 56. Ein gebautes Abbild ist genau
// Blockzahl mal Blockgroesse gross (an zwei echten .ffpfsc der Test-Konsole am 15. Nov. 2026 nachgerechnet). Gemeinsam fuer den Sammler und den Snapshot-Export.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace medic {

constexpr std::size_t kPfsHeadBytes = 64;

// Soll-Groesse des Abbilds in Byte aus den ersten kPfsHeadBytes Bytes, sonst 0 (kein gueltiger Kopf).
inline std::uint64_t pfs_volume_bytes(const std::string& head, std::uint64_t* block_size = nullptr, std::uint64_t* blocks = nullptr) {
    if (head.size() < kPfsHeadBytes) return 0;
    auto le = [&](std::size_t off, int n) {
        std::uint64_t v = 0;
        for (int i = n - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(head[off + static_cast<std::size_t>(i)]);
        return v;
    };
    const std::uint64_t bs = le(32, 4), nd = le(56, 8);
    if (le(8, 4) != 0x01332A0Bu || bs < 4096 || bs > (1u << 24) || (bs & (bs - 1)) != 0 || nd == 0 || nd >= (1ull << 40)) return 0;
    if (block_size) *block_size = bs;
    if (blocks) *blocks = nd;
    return nd * bs;
}

}  // namespace medic
