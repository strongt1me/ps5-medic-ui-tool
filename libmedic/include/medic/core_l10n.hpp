// SPDX-License-Identifier: GPL-3.0-or-later
// Französische, italienische und spanische Fassung der englischen Meldungen der Fix-Engine (Notizen, Hinweise, Warnungen, Fehler).
#pragma once
#include <string>

namespace medic {

// Übersetzt die bekannten englischen Fragmente nach "fr", "it" oder "es"; für andere Sprachen und unbekannten Text bleibt der Eingang unverändert.
std::string core_text_l10n(const std::string& en, const std::string& lang);

// true, wenn dieses englische Fragment in der Tabelle steht (für den Test).
bool core_l10n_has_entry(const std::string& en_fragment);

}  // namespace medic
