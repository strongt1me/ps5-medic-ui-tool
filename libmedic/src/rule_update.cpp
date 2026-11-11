// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/rule_update.hpp"

#include "medic/error_codes.hpp"
#include "medic/rules.hpp"
#include "medic/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

std::string join(const std::string& dir, const std::string& name) { return dir + "/" + name; }

// Was sich je Wissensdatei unterscheidet: Dateiname, aktiver Pfad, Typen im update.log und Namensanfang der Sicherungen.
struct Spec {
    const char* name;
    const char* active;
    const char* log_update;
    const char* log_rollback;
    const char* backup;
};

const Spec& spec_of(KnowledgeKind k) {
    static const Spec rules{"rules.json", kRulesPath, "rules_update", "rules_rollback", "rules"};
    static const Spec codes{"error_codes.json", kErrorCodesPath, "error_codes_update", "error_codes_rollback", "error_codes"};
    return k == KnowledgeKind::ErrorCodes ? codes : rules;
}

std::string update_rules_path(const Spec& s) { return join(kRulesUpdateDir, s.name); }
std::string update_sum_path(const Spec& s) { return join(kRulesUpdateDir, std::string(s.name) + ".sha256"); }
std::string update_sig_path(const Spec& s) { return join(kRulesUpdateDir, std::string(s.name) + ".sig"); }

bool valid_timestamp(const std::string& ts) {
    return !ts.empty() && ts.size() <= 40 &&
           std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Erstes Wort der ersten nicht leeren Zeile, wenn es genau 64 Hex-Ziffern sind (Format von `sha256sum`: "<hash>  <datei>").
std::string parse_checksum_file(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        const bool hex64 = first.size() == 64 && std::all_of(first.begin(), first.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        return hex64 ? lower(first) : std::string();
    }
    return {};
}

// update.log: eine JSON-Zeile je Ereignis. Liefert die letzte Installation, die noch nicht zurueckgenommen wurde.
bool last_open_install(const IFileSystem& fs, const Spec& s, json& out) {
    std::string text;
    if (!fs.read_file(kRulesUpdateLog, text)) return false;
    std::istringstream in(text);
    std::string line;
    bool open = false;
    json last;
    while (std::getline(in, line)) {
        json j = json::parse(line, nullptr, false);
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) continue;
        if (j["type"] == s.log_update) {
            last = j;
            open = true;
        } else if (j["type"] == s.log_rollback) {
            open = false;
        }
    }
    if (open) out = last;
    return open;
}

// Hoechste Version unter den noch nicht zurueckgenommenen Installationen laut update.log. Der Rollback-Schutz ("nicht neuer")
// vergleicht auch damit und nicht nur mit der gerade geladenen Datei: laesst sich knowledge/rules.json nicht laden und gelten
// die mitgelieferten Regeln, haette sonst eine aeltere, gueltig signierte Datei untergeschoben werden koennen.
std::string installed_high_water(const IFileSystem& fs, const Spec& s) {
    std::string text;
    if (!fs.read_file(kRulesUpdateLog, text)) return {};
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> installed;
    while (std::getline(in, line)) {
        json j = json::parse(line, nullptr, false);
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) continue;
        if (j["type"] == s.log_update && j.contains("to") && j["to"].is_string()) installed.push_back(j["to"].get<std::string>());
        else if (j["type"] == s.log_rollback && !installed.empty()) installed.pop_back();
    }
    std::string best;
    for (const auto& v : installed)
        if (best.empty() || compare_versions(v, best) > 0) best = v;
    return best;
}

bool append_log(Platform& p, const json& rec) {
    std::string text;
    p.fs().read_file(kRulesUpdateLog, text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += rec.dump() + "\n";
    return p.fs_rw()->write_file(kRulesUpdateLog, text);
}

std::string str_of(const json& j, const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(); }

}  // namespace

RuleUpdateOffer check_rule_update(const IFileSystem& fs, const std::string& current_version, std::string* verified_text,
                                  const std::vector<TrustedKey>* keys, KnowledgeKind kind) {
    const Spec& spec = spec_of(kind);
    RuleUpdateOffer o;
    o.old_version = current_version;

    if (json open; last_open_install(fs, spec, open)) {
        const std::string backup = str_of(open, "backup");
        o.can_rollback = backup.empty() ? fs.exists(spec.active) : fs.exists(backup);
        o.installed_version = str_of(open, "to");
    }

    const bool has_rules = fs.exists(update_rules_path(spec)), has_sum = fs.exists(update_sum_path(spec)), has_sig = fs.exists(update_sig_path(spec));
    if (!has_rules && !has_sum && !has_sig) return o;  // State::None
    auto reject = [&](const char* reason) {
        o.state = RuleUpdateOffer::State::Rejected;
        o.reason = reason;
        return o;
    };
    if (!has_rules) return reject("no_rules_file");

    std::string text;
    if (!fs.read_range(update_rules_path(spec), 0, kMaxRulesBytes + 1, text)) return reject("unreadable");
    if (text.size() > kMaxRulesBytes) return reject("too_large");

    o.sha256 = sha256_hex(text);
    if (has_sig) {
        // Eine Signatur entscheidet allein; eine daneben liegende Pruefsumme zaehlt nicht (sie koennte vom selben Server stammen).
        std::string sig_text;
        if (!fs.read_range(update_sig_path(spec), 0, 4096, sig_text)) return reject("unreadable");
        const SigInfo si = verify_rules_signature(text, sig_text, keys ? *keys : trusted_rule_keys());
        switch (si.result) {
            case SigResult::Valid: break;
            case SigResult::Malformed: return reject("sig_malformed");
            case SigResult::UnknownKey: return reject("sig_unknown_key");
            case SigResult::BadSignature: return reject("sig_invalid");
        }
        o.auth = "signature";
        o.signed_by = si.key_id;
    } else {
        std::string sum_text;
        if (!has_sum || !fs.read_file(update_sum_path(spec), sum_text)) return reject("no_checksum");
        const std::string expected = parse_checksum_file(sum_text);
        if (expected.empty()) return reject("no_checksum");
        if (o.sha256 != expected) return reject("checksum_mismatch");
        o.auth = "checksum";
    }

    // Formatstufe vor dem Schema: eine Datei, die Neuerungen nutzt, die diese App nicht kennt, ist nicht "ungueltig", sondern zu neu.
    if (const json head = json::parse(text, nullptr, false); head.is_object() && head.contains("requires_level") && head["requires_level"].is_number_integer() &&
                                                           head["requires_level"].get<long>() > kRuleFormatLevel)
        return reject("needs_newer_app");

    // Schema und Version: je nach Art die Regel- oder die Fehlercode-Pruefung (eine Datei der anderen Art faellt hier durch).
    try {
        if (kind == KnowledgeKind::ErrorCodes) {
            const ErrorCodeSet set = load_error_codes_from_string(text);
            o.new_version = set.db_version;
            o.new_rule_count = set.codes.size();
        } else {
            const RuleSet rs = load_rules_from_string(text);
            o.new_version = rs.db_version;
            o.new_rule_count = rs.rules.size();
        }
    } catch (const std::exception& e) {
        o.detail = e.what();
        return reject("invalid_rules");
    }
    if (compare_versions(o.new_version, current_version) <= 0) return reject("not_newer");
    if (const std::string high = installed_high_water(fs, spec); !high.empty() && compare_versions(o.new_version, high) <= 0) return reject("not_newer");

    o.state = RuleUpdateOffer::State::Ready;
    if (verified_text) *verified_text = std::move(text);
    return o;
}

RuleUpdateResult install_rule_update(Platform& p, const std::string& current_version, const std::string& timestamp,
                                     const std::vector<TrustedKey>* keys, KnowledgeKind kind) {
    const Spec& spec = spec_of(kind);
    RuleUpdateResult r;
    if (!p.fs_rw()) {
        r.error = "no_write";
        return r;
    }
    if (!valid_timestamp(timestamp)) {
        r.error = "bad_timestamp";
        return r;
    }
    std::string text;
    const RuleUpdateOffer offer = check_rule_update(p.fs(), current_version, &text, keys, kind);
    if (offer.state != RuleUpdateOffer::State::Ready) {
        r.error = offer.state == RuleUpdateOffer::State::None ? "no_rules_file" : offer.reason;
        return r;
    }

    // 1. Bisherige Datei sichern (und das Backup gegenlesen), bevor etwas veraendert wird.
    std::string active;
    std::string backup_path, backup_sha;
    if (p.fs().read_file(spec.active, active)) {
        backup_path = join(kRulesBackupDir, std::string(spec.backup) + "_" + timestamp + ".json");
        backup_sha = sha256_hex(active);
        std::string again;
        if (!p.fs_rw()->write_file(backup_path, active) || !p.fs().read_file(backup_path, again) || sha256_hex(again) != backup_sha) {
            r.error = "backup_failed";
            return r;
        }
    }

    // 2. Neue Datei atomar schreiben und gegenlesen.
    std::string written;
    if (!p.fs_rw()->write_file(spec.active, text)) {
        r.error = "write_failed";
        return r;
    }
    // Zurueck zum Zustand vor der Installation: das Backup einspielen, gab es keine Datei, die neue beiseite legen
    // (nicht aktiv lassen und nicht loeschen).
    auto restore = [&](const char* tag) {
        if (!backup_path.empty()) p.fs_rw()->write_file(spec.active, active);
        else p.fs_rw()->rename(spec.active, join(kRulesBackupDir, std::string(spec.backup) + "_" + tag + "_" + timestamp + ".json"));
    };
    if (!p.fs().read_file(spec.active, written) || sha256_hex(written) != offer.sha256) {
        restore("failed");
        r.error = "verify_failed";
        return r;
    }

    // 3. Update-Dateien beiseite legen (nie loeschen), damit sie nicht erneut angeboten werden. Scheitert das, ist es
    //    harmlos: die installierte Version ist dann nicht mehr neuer als die laufende.
    const std::string done = join(join(kRulesUpdateDir, "installed"), timestamp);
    p.fs_rw()->rename(update_rules_path(spec), join(done, spec.name));
    p.fs_rw()->rename(update_sum_path(spec), join(done, std::string(spec.name) + ".sha256"));
    p.fs_rw()->rename(update_sig_path(spec), join(done, std::string(spec.name) + ".sig"));

    // Ohne Log-Eintrag gaebe es keine Rueckgaengig-Funktion: lieber den alten Stand wiederherstellen als eine Installation, die
    // sich nicht zuruecknehmen laesst.
    if (!append_log(p, {{"type", spec.log_update}, {"ts", timestamp}, {"from", current_version}, {"to", offer.new_version},
                        {"sha256", offer.sha256}, {"auth", offer.auth}, {"key", offer.signed_by}, {"backup", backup_path}, {"backup_sha256", backup_sha}})) {
        restore("unlogged");
        r.error = "log_failed";
        return r;
    }
    r.ok = true;
    r.new_version = offer.new_version;
    r.backup_path = backup_path;
    return r;
}

RuleUpdateResult rollback_rule_update(Platform& p, const std::string& timestamp, KnowledgeKind kind) {
    const Spec& spec = spec_of(kind);
    RuleUpdateResult r;
    if (!p.fs_rw()) {
        r.error = "no_write";
        return r;
    }
    if (!valid_timestamp(timestamp)) {
        r.error = "bad_timestamp";
        return r;
    }
    json open;
    if (!last_open_install(p.fs(), spec, open)) {
        r.error = "nothing_to_roll_back";
        return r;
    }
    const std::string backup = str_of(open, "backup"), backup_sha = str_of(open, "backup_sha256");
    const std::string aside = join(kRulesBackupDir, std::string(spec.backup) + "_rolledback_" + timestamp + ".json");

    std::string current;
    if (!p.fs().read_file(spec.active, current)) {  // die installierte Datei ist nicht mehr da: nichts zu ersetzen
        if (backup.empty()) {
            r.error = "nothing_to_roll_back";
            return r;
        }
        current.clear();
    }

    if (backup.empty()) {
        // Vor dem Update gab es keine Datei im Wissensordner (es galten die mitgelieferten Regeln): beiseite legen.
        if (!p.fs_rw()->rename(spec.active, aside)) {
            r.error = "write_failed";
            return r;
        }
    } else {
        std::string old;
        if (!p.fs().read_file(backup, old) || sha256_hex(old) != backup_sha) {
            r.error = "backup_failed";
            return r;
        }
        if (!current.empty() && !p.fs_rw()->write_file(aside, current)) {
            r.error = "write_failed";
            return r;
        }
        if (!p.fs_rw()->write_file(spec.active, old)) {
            r.error = "write_failed";
            return r;
        }
        r.backup_path = aside;
    }
    append_log(p, {{"type", spec.log_rollback}, {"ts", timestamp}, {"from", str_of(open, "to")}, {"to", str_of(open, "from")}, {"aside", backup.empty() ? aside : r.backup_path}});
    r.ok = true;
    r.new_version = str_of(open, "from");
    return r;
}

}  // namespace medic
