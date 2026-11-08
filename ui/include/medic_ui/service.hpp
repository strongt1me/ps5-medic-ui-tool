// Schnittstelle zwischen UI und Medic-Kern. Die UI kennt nur dieses Interface; dahinter steckt entweder der Kern im
// selben Prozess (LocalService: PC-Vorschau, Tests, Payload) oder spaeter eine Verbindung zu einem privilegierten
// Dienst auf der Konsole (die Titel-App darf /data und /mnt nicht selbst lesen, siehe docs/PHASE3.md).
#pragma once
#include "medic/fixes.hpp"
#include "medic/platform.hpp"
#include "medic/report.hpp"
#include "medic/rule_update.hpp"
#include "medic/rules.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace medic::ui {

struct CheckResult {
    std::string fw, model, rules_version;
    std::vector<medic::Finding> findings;
};

struct ReportResult {
    bool saved = false;      // voller Bericht liegt unter `path`
    std::string path;        // leer, wenn nicht gespeichert
    std::string error;       // Kennung, warum nicht gespeichert: "no_write" | "write_failed" (leer bei Erfolg)
    std::string qr_text;     // Kurzfassung fuer den QR-Code (anonymisiert)
    bool truncated = false;  // die Kurzfassung laesst Befunde weg
};

// Groesse der Kurzfassung im QR-Code: bis Version 20 (97 Module) bleibt er vom Sofa aus mit dem Handy lesbar.
constexpr std::size_t kQrMaxBytes = 640;

class IMedicService {
public:
    virtual ~IMedicService() = default;
    virtual CheckResult run_check() = 0;
    virtual medic::FixPlan plan_fix(const medic::Finding& f) = 0;
    // Schreibt wirklich. Die UI ruft das nur nach ausdruecklicher Bestaetigung des Nutzers auf (Vorschau -> Kreuz).
    virtual medic::ApplyResult apply_fix(const medic::FixPlan& plan) = 0;
    virtual std::string read_text(const std::string& path) = 0;  // fuer die Diff-Vorschau; leer, wenn nicht lesbar
    virtual std::vector<medic::TxnInfo> history() = 0;
    virtual medic::UndoResult undo(const std::string& txn) = 0;
    // Bericht zum letzten Check-up: schreibt ihn (nur) nach /data/ps5medic/reports/ und liefert die QR-Kurzfassung.
    // Die UI ruft das nur auf Quadrat-Taste auf.
    virtual ReportResult make_report(medic::ReportLang lang) = 0;
    // Manuelles Regel-Update aus einer Datei (siehe libmedic/include/medic/rule_update.hpp). check schreibt nichts; install und
    // rollback schreiben und werden von der UI nur nach ausdruecklicher Bestaetigung (Kreuz) aufgerufen. Nach Erfolg gelten die
    // neuen bzw. wiederhergestellten Regeln sofort fuer den naechsten Check-up.
    virtual medic::RuleUpdateOffer check_rule_update() = 0;
    virtual medic::RuleUpdateResult install_rule_update() = 0;
    virtual medic::RuleUpdateResult rollback_rule_update() = 0;
};

// Kern im selben Prozess. `clock` liefert Zeitstempel fuer Backups/Journal (Standard: UTC "YYYYMMDDTHHMMSSZ").
class LocalService : public IMedicService {
public:
    LocalService(medic::Platform& platform, medic::RuleSet rules, std::function<std::string()> clock = nullptr);
    CheckResult run_check() override;
    medic::FixPlan plan_fix(const medic::Finding& f) override;
    medic::ApplyResult apply_fix(const medic::FixPlan& plan) override;
    std::string read_text(const std::string& path) override;
    std::vector<medic::TxnInfo> history() override;
    medic::UndoResult undo(const std::string& txn) override;
    ReportResult make_report(medic::ReportLang lang) override;
    medic::RuleUpdateOffer check_rule_update() override;
    medic::RuleUpdateResult install_rule_update() override;
    medic::RuleUpdateResult rollback_rule_update() override;

private:
    medic::Platform& p_;
    medic::RuleSet rules_;
    medic::Facts facts_;
    std::vector<medic::Finding> last_findings_;
    std::optional<medic::RuleSet> rules_before_install_;  // fuer den Rollback, wenn es vorher keine Datei im Wissensordner gab

    bool reload_rules();
    void ensure_facts();
    std::function<std::string()> clock_;
};

}  // namespace medic::ui
