// Fix-Engine: plan -> (Nutzer bestaetigt) -> apply -> optional undo.
//
// Sicherheitsmodell (docs/ARCHITECTURE.md, CLAUDE.md):
//  * Schreiben nur ueber apply_plan/undo_txn und nur auf Pfade der Whitelist (PathPolicy).
//  * Nichts wird geloescht: Dateien werden umbenannt/verschoben, ueberschriebene Dateien vorher gesichert
//    nach /data/ps5medic/backups/<txn>/.
//  * Jede Aenderung steht in /data/ps5medic/journal.jsonl (SHA-256 vorher/nachher).
//  * apply_plan bricht bei jedem Fehler ab und macht bereits Erledigtes rueckgaengig.
//
// Fix-Parameter (fix_params in der Regel):
//   rename_image_parentheses            keine
//   smp_set_key                         section, key, legacy_key (optional), file (Standard config.ini),
//                                       mode "set" (Standard) | "add",
//                                       value | from {array, field, agg: "max"|"distinct"}
//   smp_disable_conflicting_backpork    files [Pfade], match (Regex, Standard "backpork")
//   move_duplicate_titleid              keine (optional FixOptions::keep)
//   fix_image_nesting_hint              keine (nur Hinweis)
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <string>
#include <vector>

namespace medic {

struct Change {
    enum class Kind { Rename, WriteFile };
    Kind kind = Kind::Rename;
    std::string path;     // Quelle bzw. zu schreibende Datei
    std::string to;       // nur Rename
    std::string content;  // nur WriteFile: neuer Gesamtinhalt
    std::string note_de, note_en;
};

struct FixPlan {
    std::string fix_id, rule_id;
    std::vector<Change> changes;
    std::vector<std::string> warnings;  // z. B. uebersprungene Ziele
    std::string hint_de, hint_en;       // Anleitung (bei reinen Hinweis-Fixes)
    bool empty() const { return changes.empty(); }
};

struct FixOptions {
    std::string keep;  // move_duplicate_titleid: Pfad der Quelle, die bleiben soll
};

// Erzeugt den Plan, ohne etwas zu veraendern. Unbekannte Fixes ergeben einen leeren Plan mit Warnung.
FixPlan plan_fix(const Finding& finding, const Facts& facts, const IFileSystem& fs, const FixOptions& opt = {});

// ---- Whitelist ----------------------------------------------------------

class PathPolicy {
public:
    // extra_roots: Scanpfade aus den Fakten (smp.scan_roots). Zu breite Pfade (z. B. "/data", "/") werden ignoriert.
    explicit PathPolicy(std::vector<std::string> extra_roots = {});
    // Erlaubt sind Pfade strikt unterhalb der Whitelist-Ordner. /system, /system_ex, /dev sind immer gesperrt.
    bool allowed(const std::string& path, std::string* why = nullptr) const;
    // Entfernt doppelte/abschliessende Slashes; leer, wenn relativ oder ".." enthalten ist.
    static std::string normalize(const std::string& path);

private:
    std::vector<std::string> roots_;
};

PathPolicy policy_from_facts(const Facts& facts);

// ---- Anwenden / Rueckgaengig --------------------------------------------

struct ApplyOptions {
    std::string timestamp;   // z. B. "20261108T120000Z" (vom Aufrufer, damit testbar)
    bool confirmed = false;  // Nutzer hat den Plan gesehen und bestaetigt -- sonst wird nichts getan
};

struct ApplyResult {
    bool ok = false;
    std::string txn;    // Transaktions-ID (= Backup-Ordnername)
    std::string error;  // leer bei Erfolg
};

ApplyResult apply_plan(const FixPlan& plan, Platform& p, const PathPolicy& policy, const ApplyOptions& opt);

struct UndoResult {
    bool ok = false;
    std::string error;
};
UndoResult undo_txn(Platform& p, const PathPolicy& policy, const std::string& txn, const std::string& timestamp);

struct TxnInfo {
    std::string txn, ts, fix, rule;
    std::string state;  // applied | undone | aborted | incomplete
    int ops = 0;
};
std::vector<TxnInfo> history(const Platform& p);

constexpr const char* kJournalPath = "/data/ps5medic/journal.jsonl";
constexpr const char* kBackupRoot = "/data/ps5medic/backups";

}  // namespace medic
