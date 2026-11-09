// SPDX-License-Identifier: GPL-3.0-or-later
// Regel-Update. Zwei Wege fuehren in denselben Ordner /data/ps5medic/knowledge/update/:
//
//  * Datei-Update (ohne Netz): Der Nutzer legt rules.json und deren SHA-256 (rules.json.sha256, Format wie `sha256sum`)
//    selbst dorthin (FTP, USB-Kopie). Die Pruefsumme schuetzt vor beschaedigten oder abgeschnittenen Kopien, NICHT vor einer
//    absichtlich untergeschobenen Datei samt passender Pruefsumme (wer das kann, hat ohnehin Schreibzugriff).
//  * Download (rule_download.hpp): holt rules.json und rules.json.sig von der eingestellten Adresse in denselben Ordner.
//    Hier zaehlt NUR die Signatur: Ed25519 ueber genau die Bytes von rules.json, Schluessel aus signature.hpp. Liegt eine
//    rules.json.sig im Ordner, wird sie immer geprueft und die Pruefsumme ignoriert; eine ungueltige Signatur lehnt die Datei ab.
//
// Dasselbe gilt fuer das Fehlercode-Nachschlagewerk (error_codes.json, KnowledgeKind::ErrorCodes): eigene Dateien im selben Update-Ordner
// (error_codes.json, .sha256, .sig), dieselben Schluessel und Pruefungen, eigene Sicherungen und eigene Eintraege in update.log
// (error_codes_update / error_codes_rollback). Die Schemas unterscheiden sich, eine Regeldatei laesst sich daher nie als
// Fehlercode-Datei installieren und umgekehrt.
//
// PS5 Medic prueft, zeigt das Ergebnis und installiert erst nach ausdruecklicher Bestaetigung. Nichts wird geloescht: die
// bisherige Datei wird gesichert, die Update-Dateien wandern nach update/installed/<zeitstempel>/, jede Installation steht in
// update.log und laesst sich zuruecknehmen.
#pragma once
#include "medic/platform.hpp"
#include "medic/signature.hpp"

#include <cstddef>
#include <string>

namespace medic {

constexpr const char* kRulesPath = "/data/ps5medic/knowledge/rules.json";
constexpr const char* kRulesUpdateDir = "/data/ps5medic/knowledge/update";
constexpr const char* kRulesBackupDir = "/data/ps5medic/knowledge/backups";
constexpr const char* kRulesUpdateLog = "/data/ps5medic/knowledge/update.log";
constexpr std::size_t kMaxRulesBytes = 2u << 20;  // 2 MiB

// Welche Wissensdatei ein Update betrifft.
enum class KnowledgeKind { Rules, ErrorCodes };

struct RuleUpdateOffer {
    enum class State { None, Ready, Rejected };
    State state = State::None;       // None: keine Update-Datei; Ready: geprueft, installierbar; Rejected: siehe reason
    std::string reason;              // Kennung: no_rules_file | no_checksum | checksum_mismatch | sig_malformed | sig_unknown_key | sig_invalid
                                     //          | invalid_rules | not_newer | too_large | unreadable
    std::string detail;              // bei invalid_rules: Meldung des Regel-Pruefers
    std::string old_version;         // db_version der laufenden Regeln
    std::size_t old_rule_count = 0;  // vom Aufrufer gefuellt (der Kern kennt die laufenden Regeln nicht)
    std::string new_version;
    std::size_t new_rule_count = 0;
    std::string sha256;              // SHA-256 der geprueften Datei (hex, klein)
    std::string auth;                // bei Ready: "signature" (Ed25519, siehe signed_by) oder "checksum" (nur Pruefsumme, Datei-Update)
    std::string signed_by;           // Schluessel-ID, wenn auth == "signature"
    bool can_rollback = false;       // die letzte Installation laesst sich zuruecknehmen
    std::string installed_version;   // Version dieser letzten Installation
};

// Prueft nur (schreibt nichts). `verified_text` (optional) bekommt bei State::Ready den geprueften Dateiinhalt.
// `keys`: vertrauenswuerdige Schluessel fuer die Signatur (nullptr = die eingebauten, trusted_rule_keys()).
RuleUpdateOffer check_rule_update(const IFileSystem& fs, const std::string& current_version, std::string* verified_text = nullptr,
                                  const std::vector<TrustedKey>* keys = nullptr, KnowledgeKind kind = KnowledgeKind::Rules);

struct RuleUpdateResult {
    bool ok = false;
    std::string error;        // Kennung: no_write | bad_timestamp | nothing_to_roll_back | backup_failed | write_failed | verify_failed
                              // oder die reason aus RuleUpdateOffer
    std::string new_version;  // bei Erfolg: nach der Installation aktive Version
    std::string backup_path;  // gesicherte bisherige Datei (leer, wenn es keine gab)
};

// Installiert das Update. Prueft vorher erneut (verlaesst sich nicht auf einen frueheren check). Bricht bei jedem Fehler
// ab, ohne die aktive Datei zu veraendern.
RuleUpdateResult install_rule_update(Platform& p, const std::string& current_version, const std::string& timestamp,
                                     const std::vector<TrustedKey>* keys = nullptr, KnowledgeKind kind = KnowledgeKind::Rules);

// Nimmt die letzte Installation zurueck: stellt die gesicherte Datei wieder her (die ersetzte wird nach backups/ gelegt).
RuleUpdateResult rollback_rule_update(Platform& p, const std::string& timestamp, KnowledgeKind kind = KnowledgeKind::Rules);

}  // namespace medic
