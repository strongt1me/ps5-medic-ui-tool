// Schnittstelle zwischen UI und Medic-Kern. Die UI kennt nur dieses Interface; dahinter steckt entweder der Kern im
// selben Prozess (LocalService: PC-Vorschau, Tests, Payload) oder spaeter eine Verbindung zu einem privilegierten
// Dienst auf der Konsole (die Titel-App darf /data und /mnt nicht selbst lesen, siehe docs/PHASE3.md).
#pragma once
#include "medic/fixes.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <functional>
#include <string>
#include <vector>

namespace medic::ui {

struct CheckResult {
    std::string fw, model, rules_version;
    std::vector<medic::Finding> findings;
};

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

private:
    medic::Platform& p_;
    medic::RuleSet rules_;
    medic::Facts facts_;
    std::function<std::string()> clock_;
};

}  // namespace medic::ui
