// SPDX-License-Identifier: GPL-3.0-or-later
// Fix-Engine: plan -> (Nutzer bestaetigt) -> apply -> optional undo.
//
// Sicherheitsmodell (docs/ARCHITECTURE.md, CLAUDE.md):
//  * Schreiben nur ueber apply_plan/undo_txn und nur auf Pfade der Whitelist (PathPolicy).
//  * Nichts wird geloescht: Dateien werden umbenannt/verschoben, ueberschriebene Dateien vorher gesichert
//    nach /data/ps5medic/backups/<txn>/.
//  * Jede Aenderung steht in /data/ps5medic/journal.jsonl (SHA-256 vorher/nachher).
//  * apply_plan bricht bei jedem Fehler ab und macht bereits Erledigtes rueckgaengig.
//  * Absturzsicher: Jede Operation steht VOR der Ausfuehrung als "intent" im Journal (mit Backup-Pfad, Hashes, Wurzeln),
//    das Journal wird nur angehaengt und mit fsync gesichert, Dateien werden atomar ersetzt (Temp-Datei, fsync, Umbenennen).
//    Bleibt eine Transaktion unvollstaendig, rollt undo_txn sie anhand der Intents zurueck.
//
// Fix-Parameter (fix_params in der Regel):
//   rename_image_parentheses            keine
//   rename_case_prx                     keine; liest games.case_mismatch[] (case_sensitive 1), benennt auf den erwarteten Namen im selben Ordner um
//   smp_set_key                         section, key, legacy_key (optional), file (Standard config.ini), also (optional: weitere {section,key,legacy_key,value} derselben Datei),
//                                       mode "set" (Standard) | "add",
//                                       value | from {array, field, agg: "max"|"distinct"}
//   smp_disable_conflicting_backpork    files [Pfade], match (Regex, Standard "backpork")
//   move_duplicate_titleid              keine (optional FixOptions::keep); Ziel: <Scanpfad>/.duplicates/ (SM+ scannt Punkt-Ordner nicht)
//   smp_translate_v1_keys               keine; liest smp.v1_keys[] (flache v1-Zeilen in einer v2-config.ini)
//   autoload_insert_delay               keine; liest autoload.problems[] (kind elfldr_no_delay), fuegt "!4000" ein
//   autoload_remove_duplicates          keine; liest autoload.problems[] (kind duplicate), behaelt je Eintrag die erste Zeile
//   autoload_fix_missing                keine; liest autoload.problems[] (missing_slot, missing_file, entry_case): Schreibweise korrigieren,
//                                       sonst Zeile auskommentieren (WebKit-Liste) bzw. entfernen (Payload Manager)
//   quarantine_broken_elf               keine; liest payload.broken_elfs[], verschiebt nach <Ordner>/.medic_broken/ (nur Whitelist-Pfade)
//   quarantine_empty_image              keine; liest smp.images[] mit tiny=1 (unter 512 Byte), verschiebt nach <Ordner>/.medic_broken/ (nur Whitelist-Pfade)
//   quarantine_leftovers                keine; liest leftovers.files[] (.part, .tmp, .teil ... in den Scanpfaden), verschiebt nach <Ordner>/.medic_broken/ (derselbe Ordner,
//                                       nie loeschen; check_size bricht ab, wenn die Datei noch waechst)
//   rename_image_shorten                keine; liest smp.images[] mit name_too_long=1 (ueber 63 Byte ohne Endung, .ffpfsc 58) und kuerzt den Namen im selben Ordner (Titel-ID bleibt)
//   chmod_program_files                 keine; liest games.noexec[] (Programmdateien ohne Ausfuehrungsbit), setzt jede Datei auf 0777 (Marc, 14. Nov. 2026:
//                                       Rechte des Spielordners, gemessen: 0666 startet nicht, 0777 schon); nur Whitelist-Pfade, Journal merkt den alten Modus
//   pldmgr_enable_autoload              keine; setzt AUTOLOAD_ENABLED=1 in /data/pldmgr/pldmgr_config.txt
//   fix_image_nesting_hint              keine (nur Hinweis)
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace medic {

struct Change {
    enum class Kind { Rename, WriteFile, Chmod };
    Kind kind = Kind::Rename;
    std::string path;     // Quelle bzw. zu schreibende Datei bzw. Datei, deren Modus sich aendert
    std::string to;       // nur Rename
    std::string content;  // nur WriteFile: neuer Gesamtinhalt
    // Nur Chmod: neuer Modus (z. B. 0777) und der Modus beim Planen. Aendert sich der Modus bis zur Bestaetigung (jemand hat die Rechte inzwischen gesetzt),
    // lehnt apply_plan ab; der alte Modus kommt ins Journal, Rueckgaengig stellt ihn wieder her.
    std::uint32_t mode = 0, base_mode = 0;
    std::string note_de, note_en;
    // Nur WriteFile: Zustand der Datei, auf dem der Plan beruht. Ist check_base gesetzt, lehnt apply_plan ab, wenn sich die
    // Datei seit der Vorschau geaendert hat (ShadowMount+, FTP oder der Nutzer): sonst stuende im Diff etwas anderes als
    // tatsaechlich geschrieben wird.
    bool check_base = false;
    bool base_existed = false;
    std::string base_sha;  // SHA-256 des Inhalts beim Planen (leer, wenn die Datei fehlte)
    // Nur Rename einer DATEI (Image, ELF): Groesse beim Planen. Aendert sie sich bis zur Bestaetigung (eine Uebertragung lief noch), lehnt apply_plan
    // ab, statt eine inzwischen vollstaendige Datei beiseitezulegen.
    bool check_size = false;
    std::uint64_t base_size = 0;
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
    // extra_roots: Scanpfade aus den Fakten (smp.scan_roots). Angenommen werden nur Ordner unterhalb von /data/ sowie
    // /mnt/usb<N> und /mnt/ext<N>; zu breite (z. B. "/data", "/") und alle anderen (z. B. /user/app, /system_data,
    // /mnt/sandbox) werden ignoriert, auch wenn ein Scanpfad in der ShadowMount+-Config dorthin zeigt.
    explicit PathPolicy(std::vector<std::string> extra_roots = {});
    // Erlaubt sind Pfade strikt unterhalb der Whitelist-Ordner. /system, /system_ex, /dev sind immer gesperrt.
    bool allowed(const std::string& path, std::string* why = nullptr) const;
    // Entfernt doppelte/abschliessende Slashes; leer, wenn relativ oder ".." enthalten ist.
    static std::string normalize(const std::string& path);
    // Die angenommenen zusaetzlichen Wurzeln (ohne die festen). Das Journal merkt sie sich je Fix, damit "rueckgaengig"
    // auch dann geht, wenn der Scanpfad inzwischen aus der Config verschwunden ist.
    const std::vector<std::string>& extra_roots() const { return extra_; }

private:
    std::vector<std::string> roots_;
    std::vector<std::string> extra_;
};

PathPolicy policy_from_facts(const Facts& facts);

// Groesse einer Datei laut Eintrag im Elternordner; false bei Ordnern und fehlenden Eintraegen.
bool file_entry_size(const IFileSystem& fs, const std::string& path, std::uint64_t& size);

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
    // applied    = abgeschlossen, rueckgaengig machbar
    // undone     = zurueckgenommen
    // aborted    = Fehler beim Anwenden, alles sauber zurueckgenommen
    // incomplete = abgebrochen (Stromausfall, Absturz) oder Rollback unvollstaendig: der Zustand ist unklar, "undo_txn" stellt
    //              den Ausgangszustand wieder her (auch wiederholt, bis es klappt)
    std::string state;
    int ops = 0;
};
std::vector<TxnInfo> history(const Platform& p);

constexpr const char* kJournalPath = "/data/ps5medic/journal.jsonl";
constexpr const char* kBackupRoot = "/data/ps5medic/backups";

}  // namespace medic
