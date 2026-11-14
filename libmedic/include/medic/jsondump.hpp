// SPDX-License-Identifier: GPL-3.0-or-later
// JSON-Ausgabe, die an ungueltigem UTF-8 nicht abbricht (Dateinamen und Logzeilen der Konsole sind nicht immer gueltiges UTF-8):
// die Standard-Ausgabe von nlohmann wirft dort type_error 316, und ein Fehler beim Speichern oder Exportieren kippt den ganzen Lauf.
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace medic {

inline std::string dump_safe(const nlohmann::json& j, int indent = -1) {
    return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace medic
