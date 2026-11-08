// SPDX-License-Identifier: GPL-3.0-or-later
// Apply-/Undo-Phase der Fix-Engine: Whitelist, Backups, Journal.
#include "medic/fixes.hpp"
#include "medic/sha256.hpp"

#include <algorithm>
#include <cctype>
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

void remove_created_dirs(Platform& p, const json& rec) {
    if (!rec.contains("created_dirs") || !rec["created_dirs"].is_array()) return;
    const auto& dirs = rec["created_dirs"];
    for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
        if (!it->is_string()) continue;
        const std::string d = it->get<std::string>();
        if (p.fs().is_dir(d) && p.fs().list_dir(d).empty()) p.fs_rw()->remove_empty_dir(d);
    }
}

// ---- Journal -------------------------------------------------------------

bool read_journal(const IFileSystem& fs, std::vector<json>& out, std::string& err) {
    out.clear();
    std::string text;
    if (!fs.read_file(kJournalPath, text)) return true;  // noch kein Journal
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (!j.is_object()) {
            err = "Journal beschaedigt (Zeile " + std::to_string(n) + ") -- es wird nichts veraendert";
            return false;
        }
        out.push_back(std::move(j));
    }
    return true;
}

bool append_journal(Platform& p, const json& rec, std::string& err) {
    std::string text;
    p.fs().read_file(kJournalPath, text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += rec.dump() + "\n";
    if (!p.fs_rw()->write_file(kJournalPath, text)) {
        err = "Journal konnte nicht geschrieben werden";
        return false;
    }
    return true;
}

// ---- einzelne Operation zurueckdrehen --------------------------------------

bool revert_op(Platform& p, const json& rec, std::string& err) {
    IFileSystemRW& rw = *p.fs_rw();
    const std::string path = rec.value("path", ""), to = rec.value("to", ""), txn = rec.value("txn", "");
    if (rec.value("op", "") == "rename") {
        if (!rw.rename(to, path)) {
            err = "Rueckbenennen fehlgeschlagen: " + to + " -> " + path;
            return false;
        }
        remove_created_dirs(p, rec);
        return true;
    }
    const std::string backup = rec.value("backup", "");
    if (!backup.empty()) {
        std::string old;
        if (!p.fs().read_file(backup, old) || sha256_hex(old) != rec.value("sha_before", "")) {
            err = "Backup fehlt oder wurde veraendert: " + backup;
            return false;
        }
        if (!rw.write_file(path, old)) {
            err = "Wiederherstellen fehlgeschlagen: " + path;
            return false;
        }
        remove_created_dirs(p, rec);
        return true;
    }
    // Datei wurde neu angelegt: nicht loeschen, sondern in den Backup-Ordner verschieben.
    const std::string dest = join_path(join_path(join_path(kBackupRoot, txn), "created"), path.substr(1));
    if (!rw.rename(path, dest)) {
        err = "Neu angelegte Datei konnte nicht beiseite gelegt werden: " + path;
        return false;
    }
    remove_created_dirs(p, rec);
    return true;
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
}  // namespace

PathPolicy::PathPolicy(std::vector<std::string> extra_roots) {
    // ps5_autoloader = WebKit-Autoloader, pldmgr = Payload Manager (beide mit eigener autoload.txt)
    roots_ = {"/data/shadowmount", "/data/ps5medic", "/data/ps5_autoloader", "/data/pldmgr"};
    for (const auto& r : extra_roots) {
        std::string n = normalize(r);
        if (n.empty() || component_count(n) < 2) continue;  // "/", "/data", relative, ".." -> ignorieren
        bool denied = false;
        for (const char* d : kDenied) denied = denied || starts_with_dir(n, d);
        if (!denied) roots_.push_back(n);
    }
}

bool PathPolicy::allowed(const std::string& path, std::string* why) const {
    auto no = [&](const char* msg) {
        if (why) *why = std::string(msg) + ": " + path;
        return false;
    };
    const std::string n = normalize(path);
    if (n.empty()) return no("Ungueltiger Pfad");
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

ApplyResult apply_plan(const FixPlan& plan, Platform& p, const PathPolicy& policy, const ApplyOptions& opt) {
    ApplyResult r;
    auto fail = [&](const std::string& msg) {
        r.ok = false;
        r.error = msg;
        return r;
    };
    if (!opt.confirmed) return fail("Nicht bestaetigt: es wurde nichts geaendert");
    if (!p.fs_rw()) return fail("Kein Schreibzugriff auf dieses Dateisystem");
    if (plan.changes.empty()) return fail("Der Plan enthaelt keine Aenderungen");
    if (!valid_txn_name(opt.timestamp)) return fail("Ungueltiger Zeitstempel");

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
            if (p.fs().exists(c.to)) return fail("Ziel existiert bereits: " + c.to);
        }
    }
    std::vector<json> journal;
    std::string err;
    if (!read_journal(p.fs(), journal, err)) return fail(err);

    // 2. Transaktions-ID (eindeutig, = Backup-Ordner)
    std::set<std::string> used;
    for (const auto& j : journal) used.insert(j.value("txn", ""));
    std::string txn = opt.timestamp;
    for (int n = 2; used.count(txn); ++n) txn = opt.timestamp + "-" + std::to_string(n);
    r.txn = txn;

    // 3. Ausfuehren, jede Aenderung erst als "intent", dann als "op" ins Journal.
    std::vector<json> done;
    auto abort_all = [&](const std::string& msg) {
        std::string e2;
        for (auto it = done.rbegin(); it != done.rend(); ++it) revert_op(p, *it, e2);
        json ab = {{"type", "abort"}, {"txn", txn}, {"ts", opt.timestamp}, {"error", msg + (e2.empty() ? "" : " | Rollback: " + e2)}};
        std::string e3;
        append_journal(p, ab, e3);
        return fail(msg + (e2.empty() ? " (Aenderungen zurueckgenommen)" : " | Rollback unvollstaendig: " + e2));
    };

    int seq = 0;
    for (const Change& c : plan.changes) {
        json rec = {{"type", "op"}, {"txn", txn}, {"seq", seq}, {"ts", opt.timestamp}, {"fix", plan.fix_id}, {"rule", plan.rule_id}};
        const bool is_rename = c.kind == Change::Kind::Rename;
        rec["op"] = is_rename ? "rename" : "write";
        rec["path"] = c.path;
        if (is_rename) rec["to"] = c.to;

        json intent = {{"type", "intent"}, {"txn", txn}, {"seq", seq}, {"ts", opt.timestamp}, {"op", rec["op"]}, {"path", c.path}};
        if (is_rename) intent["to"] = c.to;
        if (!append_journal(p, intent, err)) return abort_all(err);

        if (is_rename) {
            const Stat st = stat_path(p.fs(), c.path);
            bool ok = true;
            std::string before = hash_file(p.fs(), c.path, ok);
            if (!ok) return abort_all("Quelle nicht lesbar: " + c.path);
            rec["created_dirs"] = missing_ancestors(p.fs(), c.to);
            if (!p.fs_rw()->rename(c.path, c.to)) return abort_all("Umbenennen fehlgeschlagen: " + c.path + " -> " + c.to);
            rec["kind"] = st.is_dir ? "dir" : "file";
            rec["size"] = st.size;
            // Ab hier ist die Operation passiert -> fuer Rollback vormerken, bevor weiter geprueft wird.
            rec["sha_before"] = before;
            done.push_back(rec);
            if (!p.fs().exists(c.to) || p.fs().exists(c.path)) return abort_all("Nachpruefung fehlgeschlagen: " + c.path);
            std::string after = hash_file(p.fs(), c.to, ok);
            if (!ok || after != before) return abort_all("Pruefsumme nach dem Verschieben stimmt nicht: " + c.to);
            rec["sha_after"] = after;
            done.back() = rec;
        } else {
            std::string old;
            const bool existed = p.fs().read_file(c.path, old);
            std::string backup;
            if (existed) {
                backup = join_path(join_path(kBackupRoot, txn), c.path.substr(1));
                std::string rb;
                if (!p.fs_rw()->write_file(backup, old) || !p.fs().read_file(backup, rb) || rb != old)
                    return abort_all("Backup konnte nicht angelegt werden: " + c.path);
            }
            rec["backup"] = backup;
            rec["created_dirs"] = missing_ancestors(p.fs(), c.path);
            rec["sha_before"] = existed ? sha256_hex(old) : std::string();
            rec["sha_after"] = sha256_hex(c.content);
            rec["size"] = c.content.size();
            if (!p.fs_rw()->write_file(c.path, c.content)) return abort_all("Schreiben fehlgeschlagen: " + c.path);
            done.push_back(rec);
            std::string rb;
            if (!p.fs().read_file(c.path, rb) || sha256_hex(rb) != rec["sha_after"].get<std::string>())
                return abort_all("Nachpruefung fehlgeschlagen: " + c.path);
        }
        if (!append_journal(p, rec, err)) return abort_all(err);
        ++seq;
    }

    json commit = {{"type", "commit"}, {"txn", txn}, {"ts", opt.timestamp}, {"fix", plan.fix_id}, {"rule", plan.rule_id}, {"ops", seq}};
    if (!append_journal(p, commit, err)) return abort_all(err);
    r.ok = true;
    return r;
}

// ---- undo ------------------------------------------------------------------

UndoResult undo_txn(Platform& p, const PathPolicy& policy, const std::string& txn, const std::string& timestamp) {
    UndoResult r;
    auto fail = [&](const std::string& msg) {
        r.ok = false;
        r.error = msg;
        return r;
    };
    if (!p.fs_rw()) return fail("Kein Schreibzugriff auf dieses Dateisystem");
    if (!valid_txn_name(timestamp)) return fail("Ungueltiger Zeitstempel");
    std::vector<json> journal;
    std::string err;
    if (!read_journal(p.fs(), journal, err)) return fail(err);

    std::vector<json> ops;
    bool committed = false, undone = false;
    for (const auto& j : journal) {
        if (j.value("txn", "") != txn) continue;
        const std::string type = j.value("type", "");
        if (type == "op") ops.push_back(j);
        else if (type == "commit") committed = true;
        else if (type == "undo") undone = true;
    }
    if (!committed) return fail("Transaktion unbekannt oder nicht abgeschlossen: " + txn);
    if (undone) return fail("Transaktion wurde bereits rueckgaengig gemacht: " + txn);
    std::sort(ops.begin(), ops.end(), [](const json& a, const json& b) { return a.value("seq", 0) > b.value("seq", 0); });

    // Vorpruefung: Zustand muss genau dem Zustand nach apply entsprechen, sonst wuerden wir fremde Aenderungen ueberschreiben.
    for (const auto& op : ops) {
        std::string why;
        const std::string path = op.value("path", ""), to = op.value("to", "");
        if (!policy.allowed(path, &why)) return fail(why);
        if (op.value("op", "") == "rename") {
            if (!policy.allowed(to, &why)) return fail(why);
            if (!p.fs().exists(to)) return fail("Datei fehlt am Zielort: " + to);
            if (p.fs().exists(path)) return fail("Am urspruenglichen Ort liegt inzwischen etwas: " + path);
            bool ok = true;
            std::string now = hash_file(p.fs(), to, ok);
            if (!ok || now != op.value("sha_after", "")) return fail("Datei wurde nach dem Fix veraendert: " + to);
        } else {
            std::string cur;
            if (!p.fs().read_file(path, cur)) return fail("Datei fehlt: " + path);
            if (sha256_hex(cur) != op.value("sha_after", "")) return fail("Datei wurde nach dem Fix veraendert: " + path);
            const std::string backup = op.value("backup", "");
            if (!backup.empty()) {
                std::string old;
                if (!p.fs().read_file(backup, old) || sha256_hex(old) != op.value("sha_before", ""))
                    return fail("Backup fehlt oder wurde veraendert: " + backup);
            }
        }
    }

    for (const auto& op : ops) {
        std::string e;
        if (!revert_op(p, op, e)) {
            json uf = {{"type", "undo_failed"}, {"txn", txn}, {"ts", timestamp}, {"error", e}};
            std::string e2;
            append_journal(p, uf, e2);
            return fail("Rueckgaengig machen unvollstaendig: " + e);
        }
    }
    json rec = {{"type", "undo"}, {"txn", txn}, {"ts", timestamp}, {"ops", ops.size()}};
    if (!append_journal(p, rec, err)) return fail(err);
    r.ok = true;
    return r;
}

// ---- Verlauf ---------------------------------------------------------------

std::vector<TxnInfo> history(const Platform& p) {
    std::vector<json> journal;
    std::string err;
    std::vector<TxnInfo> out;
    if (!read_journal(p.fs(), journal, err)) return out;
    auto find = [&](const std::string& txn) -> TxnInfo& {
        for (auto& t : out)
            if (t.txn == txn) return t;
        out.push_back({});
        out.back().txn = txn;
        out.back().state = "incomplete";
        return out.back();
    };
    for (const auto& j : journal) {
        const std::string type = j.value("type", ""), txn = j.value("txn", "");
        if (txn.empty()) continue;
        TxnInfo& t = find(txn);
        if (t.ts.empty()) t.ts = j.value("ts", "");
        if (t.fix.empty()) t.fix = j.value("fix", "");
        if (t.rule.empty()) t.rule = j.value("rule", "");
        if (type == "op") ++t.ops;
        else if (type == "commit") t.state = "applied";
        else if (type == "abort") t.state = "aborted";
        else if (type == "undo") t.state = "undone";
    }
    return out;
}

}  // namespace medic
