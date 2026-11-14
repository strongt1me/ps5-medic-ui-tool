// SPDX-License-Identifier: GPL-3.0-or-later
// Apply-/Undo-Phase der Fix-Engine: Whitelist, Backups, Journal.
//
// Absturzsicherheit (Stromausfall, USB abgezogen, Absturz mitten im Fix):
//  * Vor jeder Operation steht ein "intent" im Journal -- mit allem, was zum Zurueckdrehen noetig ist (Backup-Pfad, Hash vorher,
//    geplanter Hash nachher, neu anzulegende Ordner, erlaubte Wurzeln). Ein Absturz nach dem Intent laesst sich dadurch
//    zurueckrollen, auch wenn die Operation selbst noch nicht als "op" verbucht wurde.
//  * Das Journal wird nur angehaengt (append_file mit fsync); unlesbare Zeilen (Rest eines abgebrochenen Schreibvorgangs)
//    werden beim Lesen uebersprungen, der Commit-Eintrag kennt die Zahl der Operationen und deckt verlorene Zeilen auf.
//  * undo_txn stellt je Operation fest, ob sie im Zustand "nachher" (zurueckdrehen), "vorher" (schon erledigt, ueberspringen)
//    oder in einem fremden Zustand ist (abbrechen, nichts anfassen). Dadurch laesst sich ein halb gescheitertes Undo
//    und eine abgebrochene Transaktion beliebig oft wiederholen.
#include "medic/casename.hpp"
#include "medic/fixes.hpp"
#include "medic/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

constexpr std::uint64_t kMaxHashBytes = 64ull << 20;  // groessere Dateien (Images) werden bei Rename nicht gehasht
constexpr std::size_t kHashChunk = 1u << 20;

std::string join_path(const std::string& dir, const std::string& name) {
    return (!dir.empty() && dir.back() == '/') ? dir + name : dir + "/" + name;
}
std::string dir_of(const std::string& p) {
    auto pos = p.rfind('/');
    return pos == std::string::npos || pos == 0 ? "/" : p.substr(0, pos);
}
std::string base_of(const std::string& p) {
    auto pos = p.rfind('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
}
bool starts_with_dir(const std::string& path, const std::string& dir) {
    return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
}

// Journalfelder koennen von Hand oder durch einen Absturz beschaedigt sein; json::value() wuerfe bei falschem Typ.
std::string str_of(const json& j, const char* key) {
    if (!j.is_object()) return {};
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
long long int_of(const json& j, const char* key, long long def) {
    if (!j.is_object()) return def;
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<long long>() : def;
}
bool bool_of(const json& j, const char* key, bool def) {
    if (!j.is_object()) return def;
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}
std::vector<std::string> strings_of(const json& j, const char* key) {
    std::vector<std::string> v;
    if (j.is_object()) {
        const auto it = j.find(key);
        if (it != j.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_string()) v.push_back(e.get<std::string>());
    }
    return v;
}

// Pfade koennen ungueltiges UTF-8 enthalten; dump() wuerfe sonst eine Ausnahme.
std::string dump_line(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

struct Stat {
    bool exists = false, is_dir = false;
    std::uint64_t size = 0;
};

Stat stat_path(const IFileSystem& fs, const std::string& path) {
    Stat s;
    if (!fs.exists(path)) return s;
    s.exists = true;
    s.is_dir = fs.is_dir(path);
    if (!s.is_dir)
        for (const auto& e : fs.list_dir(dir_of(path)))
            if (e.name == base_of(path)) s.size = e.size;
    return s;
}

// Hash einer Datei in Bloecken. Leer = Ordner oder zu gross (kein Fehler). ok=false bei Lesefehler.
std::string hash_file(const IFileSystem& fs, const std::string& path, bool& ok) {
    ok = true;
    Stat st = stat_path(fs, path);
    if (!st.exists) {
        ok = false;
        return {};
    }
    if (st.is_dir || st.size > kMaxHashBytes) return {};
    Sha256 h;
    std::string buf;
    for (std::uint64_t off = 0; off < st.size;) {
        if (!fs.read_range(path, off, kHashChunk, buf)) {
            ok = false;
            return {};
        }
        h.update(buf.data(), buf.size());
        off += buf.size();
    }
    return h.hex();
}

bool valid_txn_name(const std::string& s) {
    return !s.empty() && s.size() <= 40 &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

// Ordner, die auf dem Weg zu `path` noch fehlen (aeusserster zuerst). write_file/rename legen sie an;
// fuers Rueckgaengigmachen merken wir sie uns, damit nichts Leeres zurueckbleibt.
json missing_ancestors(const IFileSystem& fs, const std::string& path) {
    std::vector<std::string> v;
    for (std::string d = dir_of(path); d != "/" && !d.empty() && !fs.exists(d); d = dir_of(d)) v.push_back(d);
    std::reverse(v.begin(), v.end());
    return json(v);
}

// Entfernt leere Ordner, die der Fix angelegt hatte. Nur innerhalb der Whitelist (ein Journal kann veraendert sein).
void remove_created_dirs(Platform& p, const PathPolicy& policy, const json& rec) {
    const auto dirs = strings_of(rec, "created_dirs");
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
        if (!policy.allowed(*it)) continue;
        if (p.fs().is_dir(*it) && p.fs().list_dir(*it).empty()) p.fs_rw()->remove_empty_dir(*it);
    }
}

// ---- Journal -------------------------------------------------------------

// Liest alle lesbaren Zeilen. Unlesbare Zeilen werden uebersprungen: Nach einem Stromausfall mitten im Schreiben bleibt ein
// abgeschnittenes Stueck zurueck, und die naechsten Eintraege beginnen in einer neuen Zeile; ein einziger Rest darf weder
// den Verlauf noch jedes weitere Reparieren/Zurueckholen dauerhaft sperren. Dass dabei kein Eintrag still verloren geht, sichert
// der Commit-Eintrag ab: er nennt die Zahl der Operationen, und undo_txn lehnt ein Journal mit weniger Eintraegen ab.
// `err` beschreibt uebersprungene Zeilen (leer, wenn alles lesbar war); der Rueckgabewert ist false nur bei Lesefehlern.
bool read_journal(const IFileSystem& fs, std::vector<json>& out, std::string& err) {
    out.clear();
    err.clear();
    std::string text;
    if (!fs.read_file(kJournalPath, text)) {
        if (!fs.exists(kJournalPath)) return true;  // noch kein Journal
        err = "Journal vorhanden, aber nicht lesbar";  // nicht als "leer" ausgeben: Backups koennen existieren
        return false;
    }
    std::istringstream in(text);
    std::string line;
    int skipped = 0;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (!j.is_object()) {
            ++skipped;
            continue;
        }
        out.push_back(std::move(j));
    }
    if (skipped > 0) err = std::to_string(skipped) + " unlesbare Journalzeile(n) übersprungen";
    return true;
}

bool append_journal(Platform& p, const json& rec, std::string& err) {
    const std::string line = dump_line(rec) + "\n";
    if (p.fs_rw()->append_file(kJournalPath, line)) return true;
    // Dateisystem ohne append_file (z. B. Test-Huellen): die ganze Datei neu schreiben.
    std::string text;
    p.fs().read_file(kJournalPath, text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += line;
    if (!p.fs_rw()->write_file(kJournalPath, text)) {
        err = "Journal konnte nicht geschrieben werden";
        return false;
    }
    return true;
}

// ---- einzelne Operation zurueckdrehen --------------------------------------

// `rec` ist der "op"-Eintrag, oder -- wenn der Fix vor dem Verbuchen abbrach -- der "intent"-Eintrag. Beide tragen die Felder
// path, to, backup, sha_before, existed, created_dirs; nur der op-Eintrag hat zusaetzlich sha_after fuer Renames.
bool revert_op(Platform& p, const PathPolicy& policy, const std::string& txn, const json& rec, std::string& err) {
    IFileSystemRW& rw = *p.fs_rw();
    const std::string path = str_of(rec, "path"), to = str_of(rec, "to");
    if (str_of(rec, "op") == "rename") {
        if (!rw.rename(to, path)) {
            err = "Zurückbenennen fehlgeschlagen: " + to + " -> " + path;
            return false;
        }
        remove_created_dirs(p, policy, rec);
        return true;
    }
    const std::string backup = str_of(rec, "backup");
    if (!backup.empty()) {
        const std::string nb = PathPolicy::normalize(backup);
        if (nb.empty() || !starts_with_dir(nb, kBackupRoot)) {
            err = "Backup-Pfad liegt außerhalb des Backup-Ordners: " + backup;
            return false;
        }
        std::string old;
        if (!p.fs().read_file(nb, old) || sha256_hex(old) != str_of(rec, "sha_before")) {
            err = "Backup fehlt oder wurde verändert: " + backup;
            return false;
        }
        if (!rw.write_file(path, old)) {
            err = "Wiederherstellen fehlgeschlagen: " + path;
            return false;
        }
        remove_created_dirs(p, policy, rec);
        return true;
    }
    // Datei wurde neu angelegt: nicht loeschen, sondern in den Backup-Ordner verschieben.
    const std::string dest = join_path(join_path(join_path(kBackupRoot, txn), "created"), path.substr(1));
    if (!rw.rename(path, dest)) {
        err = "Neu angelegte Datei konnte nicht beiseite gelegt werden: " + path;
        return false;
    }
    remove_created_dirs(p, policy, rec);
    return true;
}

enum class OpState { Before, After, Other };

// Wo steht die Operation gerade? After = Fix-Ergebnis liegt vor (zurueckdrehen), Before = Ausgangszustand (nichts zu tun),
// Other = etwas anderes (fremde Aenderung oder unklar): dann wird nichts angefasst.
OpState classify(const Platform& p, const PathPolicy& policy, const json& r, std::string& why) {
    const std::string path = str_of(r, "path"), to = str_of(r, "to");
    if (!policy.allowed(path, &why)) return OpState::Other;
    if (str_of(r, "op") == "rename") {
        if (!policy.allowed(to, &why)) return OpState::Other;
        const bool case_only = is_case_only_rename(path, to);  // exFAT/FAT: nur der Ordnereintrag sagt, welche Schreibweise gilt
        const bool path_exists = case_only ? dir_has_name(p.fs(), path) : p.fs().exists(path);
        const bool to_exists = case_only ? dir_has_name(p.fs(), to) : p.fs().exists(to);
        if (!path_exists && to_exists) {
            const std::string sha_after = str_of(r, "sha_after");
            if (!sha_after.empty()) {
                bool ok = true;
                const std::string now = hash_file(p.fs(), to, ok);
                if (!ok || now != sha_after) {
                    why = "Datei wurde nach dem Fix verändert: " + to;
                    return OpState::Other;
                }
            }
            return OpState::After;
        }
        if (path_exists && !to_exists) return OpState::Before;
        why = path_exists ? "Am ursprünglichen Ort liegt inzwischen etwas: " + path : "Datei fehlt am Zielort: " + to;
        return OpState::Other;
    }
    std::string cur;
    const bool has = p.fs().read_file(path, cur);
    const std::string cur_sha = has ? sha256_hex(cur) : std::string();
    const std::string sha_after = str_of(r, "sha_after"), sha_before = str_of(r, "sha_before"), backup = str_of(r, "backup");
    const bool existed = bool_of(r, "existed", !backup.empty());
    if (has && !sha_after.empty() && cur_sha == sha_after) {
        if (existed) {  // Das Backup muss noch da sein, sonst gaebe es nichts zurueckzuspielen
            std::string old;
            if (backup.empty() || !p.fs().read_file(PathPolicy::normalize(backup), old) || sha256_hex(old) != sha_before) {
                why = "Backup fehlt oder wurde verändert: " + backup;
                return OpState::Other;
            }
        }
        return OpState::After;
    }
    if (existed ? (has && cur_sha == sha_before) : !has) return OpState::Before;
    why = has ? "Datei wurde nach dem Fix verändert: " + path : "Datei fehlt: " + path;
    return OpState::Other;
}

}  // namespace

// ---- PathPolicy ------------------------------------------------------------

std::string PathPolicy::normalize(const std::string& path) {
    if (path.empty() || path[0] != '/') return {};
    std::string out;
    std::stringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == ".." || seg.find('\\') != std::string::npos) return {};
        out += "/" + seg;
    }
    return out.empty() ? "/" : out;
}

namespace {
const char* const kDenied[] = {"/system", "/system_ex", "/dev"};
std::size_t component_count(const std::string& norm) {
    return static_cast<std::size_t>(std::count(norm.begin(), norm.end(), '/'));
}
// Zusaetzliche Wurzeln (Scanpfade) nur auf Datenbereichen: /data/<...>, /mnt/usb<N>[/...], /mnt/ext<N>[/...].
// Alles andere (/user/app, /system_data, /preinst, /mnt/sandbox, ...) bleibt gesperrt, auch wenn die ShadowMount+-Config
// einen solchen Scanpfad enthaelt.
bool extra_root_allowed(const std::string& n) {
    if (starts_with_dir(n, "/data")) return n != "/data";
    for (const char* prefix : {"/mnt/usb", "/mnt/ext"}) {
        const std::string pre = prefix;
        if (n.compare(0, pre.size(), pre) != 0) continue;
        std::size_t i = pre.size();
        const std::size_t digits = i;
        while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i]))) ++i;
        if (i > digits && (i == n.size() || n[i] == '/')) return true;
    }
    return false;
}
}  // namespace

PathPolicy::PathPolicy(std::vector<std::string> extra_roots) {
    // ps5_autoloader = WebKit-Autoloader, pldmgr = Payload Manager (beide mit eigener autoload.txt)
    roots_ = {"/data/shadowmount", "/data/ps5medic", "/data/ps5_autoloader", "/data/pldmgr"};
    for (const auto& r : extra_roots) {
        std::string n = normalize(r);
        if (n.empty() || component_count(n) < 2) continue;  // "/", "/data", relative, ".." -> ignorieren
        bool denied = false;
        for (const char* d : kDenied) denied = denied || starts_with_dir(n, d);
        if (denied || !extra_root_allowed(n)) continue;
        if (std::find(extra_.begin(), extra_.end(), n) != extra_.end()) continue;
        roots_.push_back(n);
        extra_.push_back(n);
    }
}

bool PathPolicy::allowed(const std::string& path, std::string* why) const {
    auto no = [&](const char* msg) {
        if (why) *why = std::string(msg) + ": " + path;
        return false;
    };
    const std::string n = normalize(path);
    if (n.empty()) return no("Ungültiger Pfad");
    for (const char* d : kDenied)
        if (starts_with_dir(n, d)) return no("Gesperrter Systempfad");
    for (const auto& r : roots_)
        if (n.size() > r.size() && starts_with_dir(n, r)) return true;
    return no("Pfad nicht in der Whitelist");
}

PathPolicy policy_from_facts(const Facts& facts) {
    std::vector<std::string> roots;
    if (const auto* v = facts.get("smp.scan_roots"); v && v->is_array())
        for (const auto& r : *v)
            if (r.is_string()) roots.push_back(r.get<std::string>());
    return PathPolicy(std::move(roots));
}

// ---- apply -----------------------------------------------------------------

namespace {

ApplyResult apply_impl(const FixPlan& plan, Platform& p, const PathPolicy& policy, const ApplyOptions& opt) {
    ApplyResult r;
    auto fail = [&](const std::string& msg) {
        r.ok = false;
        r.error = msg;
        return r;
    };
    if (!opt.confirmed) return fail("Nicht bestätigt: es wurde nichts geändert");
    if (!p.fs_rw()) return fail("Kein Schreibzugriff auf dieses Dateisystem");
    if (plan.changes.empty()) return fail("Der Plan enthält keine Änderungen");
    if (!valid_txn_name(opt.timestamp)) return fail("Ungültiger Zeitstempel");

    // 1. Alles pruefen, bevor etwas veraendert wird.
    std::set<std::string> paths_used;
    for (const Change& c : plan.changes) {
        std::string why;
        if (!policy.allowed(c.path, &why)) return fail(why);
        if (!paths_used.insert(PathPolicy::normalize(c.path)).second) return fail("Pfad mehrfach im Plan: " + c.path);
        if (c.kind == Change::Kind::Rename) {
            if (!policy.allowed(c.to, &why)) return fail(why);
            if (!paths_used.insert(PathPolicy::normalize(c.to)).second) return fail("Pfad mehrfach im Plan: " + c.to);
            if (!p.fs().exists(c.path)) return fail("Quelle existiert nicht: " + c.path);
            // Nur die Schreibweise aendern (exFAT/FAT): dort "findet" exists() das Ziel auch, wenn es dieselbe Datei ist; entscheidend ist der Ordnereintrag
            const bool case_only = is_case_only_rename(c.path, c.to);
            if (case_only && !dir_has_name(p.fs(), c.path)) return fail("Die Quelle hat nicht die erwartete Schreibweise: " + c.path);
            if (case_only ? dir_has_name(p.fs(), c.to) : p.fs().exists(c.to)) return fail("Ziel existiert bereits: " + c.to);
            if (c.check_size) {
                std::uint64_t now_size = 0;
                if (!file_entry_size(p.fs(), c.path, now_size) || now_size != c.base_size)
                    return fail("Die Datei hat sich seit der Vorschau geändert (bitte neu prüfen): " + c.path);
            }
        } else if (c.check_base) {
            // Die Datei darf sich seit der Vorschau nicht geaendert haben.
            std::string cur;
            const bool has = p.fs().read_file(c.path, cur);
            if (has != c.base_existed || (has && sha256_hex(cur) != c.base_sha))
                return fail("Die Datei hat sich seit der Vorschau geändert (bitte neu prüfen): " + c.path);
        }
    }
    std::vector<json> journal;
    std::string err;
    if (!read_journal(p.fs(), journal, err)) return fail(err);

    // 2. Transaktions-ID (eindeutig, = Backup-Ordner). Ein vorhandener Backup-Ordner wird nie wiederverwendet.
    std::set<std::string> used;
    for (const auto& j : journal) used.insert(str_of(j, "txn"));
    std::string txn = opt.timestamp;
    for (int n = 2; used.count(txn) || p.fs().exists(join_path(kBackupRoot, txn)); ++n) txn = opt.timestamp + "-" + std::to_string(n);
    r.txn = txn;

    // 3. Ausfuehren, jede Aenderung erst als "intent", dann als "op" ins Journal.
    std::vector<json> done;
    auto abort_all = [&](const std::string& msg) {
        std::string e2;
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::string e;
            if (!revert_op(p, policy, txn, *it, e)) e2 = e;
        }
        json ab = {{"type", "abort"}, {"txn", txn}, {"ts", opt.timestamp}, {"clean", e2.empty()},
                   {"error", msg + (e2.empty() ? "" : " | Rollback: " + e2)}};
        std::string e3;
        append_journal(p, ab, e3);
        return fail(msg + (e2.empty() ? " (Änderungen zurückgenommen)" : " | Rollback unvollständig: " + e2 + " -- im Verlauf zurückholen"));
    };

    const json roots = json(policy.extra_roots());
    int seq = 0;
    for (const Change& c : plan.changes) {
        json rec = {{"type", "op"}, {"txn", txn}, {"seq", seq}, {"ts", opt.timestamp}, {"fix", plan.fix_id}, {"rule", plan.rule_id}};
        const bool is_rename = c.kind == Change::Kind::Rename;
        rec["op"] = is_rename ? "rename" : "write";
        rec["path"] = c.path;
        if (is_rename) rec["to"] = c.to;

        json intent = {{"type", "intent"}, {"txn", txn}, {"seq", seq}, {"ts", opt.timestamp}, {"op", rec["op"]}, {"path", c.path}, {"roots", roots}};

        if (is_rename) {
            const Stat st = stat_path(p.fs(), c.path);
            bool ok = true;
            const std::string before = hash_file(p.fs(), c.path, ok);
            if (!ok) return abort_all("Quelle nicht lesbar: " + c.path);
            rec["created_dirs"] = missing_ancestors(p.fs(), c.to);
            intent["to"] = c.to;
            intent["created_dirs"] = rec["created_dirs"];
            intent["sha_before"] = before;
            if (!append_journal(p, intent, err)) return abort_all(err);

            if (!p.fs_rw()->rename(c.path, c.to)) return abort_all("Umbenennen fehlgeschlagen: " + c.path + " -> " + c.to);
            rec["kind"] = st.is_dir ? "dir" : "file";
            rec["size"] = st.size;
            // Ab hier ist die Operation passiert -> fuer Rollback vormerken, bevor weiter geprueft wird.
            rec["sha_before"] = before;
            done.push_back(rec);
            const bool case_only = is_case_only_rename(c.path, c.to);
            const bool there = case_only ? dir_has_name(p.fs(), c.to) : p.fs().exists(c.to);
            const bool gone = case_only ? !dir_has_name(p.fs(), c.path) : !p.fs().exists(c.path);
            if (!there || !gone) return abort_all("Nachprüfung fehlgeschlagen: " + c.path);
            const std::string after = hash_file(p.fs(), c.to, ok);
            if (!ok || after != before) return abort_all("Prüfsumme nach dem Verschieben stimmt nicht: " + c.to);
            rec["sha_after"] = after;
            done.back() = rec;
        } else {
            std::string old;
            const bool existed = p.fs().read_file(c.path, old);
            // Vorhanden, aber nicht lesbar: kein Backup moeglich, also nicht ueberschreiben (sonst waere die Datei ohne Undo verloren)
            if (!existed && p.fs().exists(c.path)) return abort_all("Datei nicht lesbar, kein Backup möglich: " + c.path);
            const std::string backup = existed ? join_path(join_path(kBackupRoot, txn), c.path.substr(1)) : std::string();
            rec["backup"] = backup;
            rec["existed"] = existed;
            rec["created_dirs"] = missing_ancestors(p.fs(), c.path);
            rec["sha_before"] = existed ? sha256_hex(old) : std::string();
            rec["sha_after"] = sha256_hex(c.content);
            rec["size"] = c.content.size();
            // Der Intent kennt alles, was ein Rollback braucht -- auch Backup-Pfad und Hashes, bevor das Backup existiert.
            intent["backup"] = rec["backup"];
            intent["existed"] = existed;
            intent["created_dirs"] = rec["created_dirs"];
            intent["sha_before"] = rec["sha_before"];
            intent["sha_after"] = rec["sha_after"];
            if (!append_journal(p, intent, err)) return abort_all(err);

            if (existed) {
                std::string rb;
                if (!p.fs_rw()->write_file(backup, old) || !p.fs().read_file(backup, rb) || rb != old)
                    return abort_all("Backup konnte nicht angelegt werden: " + c.path);
            }
            if (!p.fs_rw()->write_file(c.path, c.content)) return abort_all("Schreiben fehlgeschlagen: " + c.path);
            done.push_back(rec);
            std::string rb;
            if (!p.fs().read_file(c.path, rb) || sha256_hex(rb) != rec["sha_after"].get<std::string>())
                return abort_all("Nachprüfung fehlgeschlagen: " + c.path);
        }
        if (!append_journal(p, rec, err)) return abort_all(err);
        ++seq;
    }

    json commit = {{"type", "commit"}, {"txn", txn}, {"ts", opt.timestamp}, {"fix", plan.fix_id}, {"rule", plan.rule_id}, {"ops", seq}};
    if (!append_journal(p, commit, err)) return abort_all(err);
    r.ok = true;
    return r;
}

UndoResult undo_impl(Platform& p, const PathPolicy& policy_now, const std::string& txn, const std::string& timestamp) {
    UndoResult r;
    auto fail = [&](const std::string& msg) {
        r.ok = false;
        r.error = msg;
        return r;
    };
    if (!p.fs_rw()) return fail("Kein Schreibzugriff auf dieses Dateisystem");
    if (!valid_txn_name(timestamp)) return fail("Ungültiger Zeitstempel");
    if (!valid_txn_name(txn)) return fail("Ungültige Transaktions-ID");
    std::vector<json> journal;
    std::string err;
    if (!read_journal(p.fs(), journal, err)) return fail(err);

    std::map<long long, json> intents, ops;
    bool committed = false, undone = false, aborted_clean = false;
    long long commit_ops = -1;
    for (const auto& j : journal) {
        if (str_of(j, "txn") != txn) continue;
        const std::string type = str_of(j, "type");
        const long long seq = int_of(j, "seq", -1);
        if (type == "intent" && seq >= 0) intents[seq] = j;
        else if (type == "op" && seq >= 0) ops[seq] = j;
        else if (type == "commit") {
            committed = true;
            commit_ops = int_of(j, "ops", -1);
        }
        else if (type == "undo") undone = true;
        else if (type == "abort") aborted_clean = bool_of(j, "clean", true);
    }
    if (undone) return fail("Transaktion wurde bereits rückgängig gemacht: " + txn);
    if (!committed && aborted_clean) return fail("Transaktion war fehlgeschlagen und wurde schon zurückgenommen: " + txn);
    if (intents.empty() && ops.empty()) return fail("Transaktion unbekannt: " + txn);

    // Whitelist: heutige Wurzeln plus die, die beim Fix galten (der Scanpfad kann inzwischen aus der Config verschwunden sein).
    // Der Filter des PathPolicy-Konstruktors gilt auch fuer die Wurzeln aus dem Journal.
    std::vector<std::string> roots = policy_now.extra_roots();
    for (const auto& [seq, in] : intents)
        for (const auto& root : strings_of(in, "roots")) roots.push_back(root);
    const PathPolicy policy(std::move(roots));

    // Je Operation der "op"-Eintrag, sonst der "intent" (der Fix brach vor dem Verbuchen ab); neueste zuerst.
    std::map<long long, json> recs = intents;
    for (const auto& [seq, op] : ops) recs[seq] = op;
    if (committed && recs.empty()) return fail("Transaktion unbekannt: " + txn);
    // Fehlen Eintraege (unlesbare Journalzeilen), wuerde ein Teil der Aenderungen unbemerkt bleiben: lieber nichts tun.
    if (committed && commit_ops >= 0 && static_cast<long long>(recs.size()) < commit_ops)
        return fail("Journal unvollständig (" + std::to_string(recs.size()) + "/" + std::to_string(commit_ops) +
                    " Operationen lesbar) -- es wird nichts verändert: " + txn);

    // Vorpruefung: nichts anfassen, wenn eine Operation in einem fremden Zustand ist (fremde Aenderung nicht ueberschreiben).
    std::vector<std::pair<long long, OpState>> states;
    for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
        std::string why;
        const OpState s = classify(p, policy, it->second, why);
        if (s == OpState::Other) return fail(why);
        states.emplace_back(it->first, s);
    }

    for (const auto& [seq, s] : states) {
        const json& rec = recs[seq];
        if (s == OpState::Before) {  // schon zurueckgedreht (frueherer Versuch) -- nur uebrig gebliebene leere Ordner aufraeumen
            remove_created_dirs(p, policy, rec);
            continue;
        }
        std::string e;
        if (!revert_op(p, policy, txn, rec, e)) {
            json uf = {{"type", "undo_failed"}, {"txn", txn}, {"ts", timestamp}, {"error", e}};
            std::string e2;
            append_journal(p, uf, e2);
            return fail("Rückgängig machen unvollständig (erneut versuchen möglich): " + e);
        }
    }
    json rec = {{"type", "undo"}, {"txn", txn}, {"ts", timestamp}, {"ops", recs.size()}, {"recovered", !committed}};
    if (!append_journal(p, rec, err)) return fail(err);
    r.ok = true;
    return r;
}

}  // namespace

ApplyResult apply_plan(const FixPlan& plan, Platform& p, const PathPolicy& policy, const ApplyOptions& opt) {
    try {
        return apply_impl(plan, p, policy, opt);
    } catch (const std::exception& e) {
        ApplyResult r;
        r.error = std::string("Interner Fehler beim Anwenden: ") + e.what();
        return r;
    }
}

UndoResult undo_txn(Platform& p, const PathPolicy& policy, const std::string& txn, const std::string& timestamp) {
    try {
        return undo_impl(p, policy, txn, timestamp);
    } catch (const std::exception& e) {
        UndoResult r;
        r.error = std::string("Interner Fehler beim Rückgängigmachen: ") + e.what();
        return r;
    }
}

// ---- Verlauf ---------------------------------------------------------------

std::vector<TxnInfo> history(const Platform& p) {
    std::vector<json> journal;
    std::string err;
    std::vector<TxnInfo> out;
    try {
        read_journal(p.fs(), journal, err);  // bei beschaedigter Zeile: die lesbaren Eintraege zeigen
        auto find = [&](const std::string& txn) -> TxnInfo& {
            for (auto& t : out)
                if (t.txn == txn) return t;
            out.push_back({});
            out.back().txn = txn;
            out.back().state = "incomplete";
            return out.back();
        };
        for (const auto& j : journal) {
            const std::string type = str_of(j, "type"), txn = str_of(j, "txn");
            if (txn.empty()) continue;
            TxnInfo& t = find(txn);
            if (t.ts.empty()) t.ts = str_of(j, "ts");
            if (t.fix.empty()) t.fix = str_of(j, "fix");
            if (t.rule.empty()) t.rule = str_of(j, "rule");
            if (type == "op") ++t.ops;
            else if (type == "commit") t.state = "applied";
            else if (type == "abort") t.state = bool_of(j, "clean", true) ? "aborted" : "incomplete";  // Rollback unvollstaendig -> zurueckholen
            else if (type == "undo") t.state = "undone";
        }
    } catch (const std::exception&) {
        out.clear();
    }
    return out;
}

}  // namespace medic
