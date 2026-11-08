// Manuelles Regel-Update ohne Netzwerk.
//
// Ablauf: Der Nutzer legt eine neue rules.json und deren SHA-256 (rules.json.sha256, Format wie `sha256sum`) nach
// /data/ps5medic/knowledge/update/ (per FTP oder von USB kopiert). PS5 Medic prueft, zeigt das Ergebnis und installiert
// erst nach ausdruecklicher Bestaetigung. Nichts wird geloescht: die bisherige Datei wird gesichert, die Update-Dateien
// wandern nach update/installed/<zeitstempel>/, jede Installation steht in update.log und laesst sich zuruecknehmen.
//
// Die Pruefsumme schuetzt vor beschaedigten oder abgeschnittenen Kopien, NICHT vor einer absichtlich untergeschobenen
// Datei samt passender Pruefsumme (wer das kann, hat ohnehin Schreibzugriff auf die Konsole). Ein Download aus dem
// Netz braeuchte zusaetzlich eine Signatur und ist nicht Teil dieses Moduls.
#pragma once
#include "medic/platform.hpp"

#include <cstddef>
#include <string>

namespace medic {

constexpr const char* kRulesPath = "/data/ps5medic/knowledge/rules.json";
constexpr const char* kRulesUpdateDir = "/data/ps5medic/knowledge/update";
constexpr const char* kRulesBackupDir = "/data/ps5medic/knowledge/backups";
constexpr const char* kRulesUpdateLog = "/data/ps5medic/knowledge/update.log";
constexpr std::size_t kMaxRulesBytes = 2u << 20;  // 2 MiB

struct RuleUpdateOffer {
    enum class State { None, Ready, Rejected };
    State state = State::None;       // None: keine Update-Datei; Ready: geprueft, installierbar; Rejected: siehe reason
    std::string reason;              // Kennung: no_rules_file | no_checksum | checksum_mismatch | invalid_rules | not_newer | too_large | unreadable
    std::string detail;              // bei invalid_rules: Meldung des Regel-Pruefers
    std::string old_version;         // db_version der laufenden Regeln
    std::size_t old_rule_count = 0;  // vom Aufrufer gefuellt (der Kern kennt die laufenden Regeln nicht)
    std::string new_version;
    std::size_t new_rule_count = 0;
    std::string sha256;              // SHA-256 der geprueften Datei (hex, klein)
    bool can_rollback = false;       // die letzte Installation laesst sich zuruecknehmen
    std::string installed_version;   // Version dieser letzten Installation
};

// Prueft nur (schreibt nichts). `verified_text` (optional) bekommt bei State::Ready den geprueften Dateiinhalt.
RuleUpdateOffer check_rule_update(const IFileSystem& fs, const std::string& current_version, std::string* verified_text = nullptr);

struct RuleUpdateResult {
    bool ok = false;
    std::string error;        // Kennung: no_write | bad_timestamp | nothing_to_roll_back | backup_failed | write_failed | verify_failed
                              // oder die reason aus RuleUpdateOffer
    std::string new_version;  // bei Erfolg: nach der Installation aktive Version
    std::string backup_path;  // gesicherte bisherige Datei (leer, wenn es keine gab)
};

// Installiert das Update. Prueft vorher erneut (verlaesst sich nicht auf einen frueheren check). Bricht bei jedem Fehler
// ab, ohne die aktive Datei zu veraendern.
RuleUpdateResult install_rule_update(Platform& p, const std::string& current_version, const std::string& timestamp);

// Nimmt die letzte Installation zurueck: stellt die gesicherte Datei wieder her (die ersetzte wird nach backups/ gelegt).
RuleUpdateResult rollback_rule_update(Platform& p, const std::string& timestamp);

}  // namespace medic
