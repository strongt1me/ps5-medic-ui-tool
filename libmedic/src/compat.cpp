// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/compat.hpp"

#include <algorithm>
#include <cstring>

namespace medic {

const CompatEntry* find_compat(const std::string& titleid) {
    const CompatEntry* end = kCompatTable + kCompatCount;
    const CompatEntry* it = std::lower_bound(kCompatTable, end, titleid, [](const CompatEntry& e, const std::string& id) { return std::strcmp(e.titleid, id.c_str()) < 0; });
    return it != end && titleid == it->titleid ? it : nullptr;
}

}  // namespace medic
